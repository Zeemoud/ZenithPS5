// SPDX-License-Identifier: GPL-2.0-only
#include "cpu/crash_handler.hpp"

#include <csignal>
#include <cstring>
#include <initializer_list>
#include <ucontext.h>
#include <unistd.h>

namespace zenith::cpu {
namespace {

thread_local const GuestCallInfo* currentCall = nullptr;

// Alternate stack of the main thread, in static storage: no allocation is possible inside the handler.
alignas(16) char alternateStack[64 * 1024];

// Everything below runs inside a signal handler: no allocation, no locks, no stdio, no std::format.
// Output is built in a small buffer and written with write(2).
class Writer {
public:
    Writer() = default;
    ~Writer() { flush(); }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    void text(const char* string) {
        while (*string != '\0') {
            put(*string++);
        }
    }

    void hex(u64 value, int digits) {
        text("0x");
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            put("0123456789abcdef"[(value >> shift) & 0xF]);
        }
    }

    void byte(u8 value) {
        put("0123456789abcdef"[value >> 4]);
        put("0123456789abcdef"[value & 0xF]);
    }

    void padTo(usize column, usize written) {
        for (usize i = written; i < column; ++i) {
            put(' ');
        }
    }

    void flush() {
        if (size_ > 0) {
            const ssize_t written = write(STDERR_FILENO, buffer_, size_);
            static_cast<void>(written);
            size_ = 0;
        }
    }

private:
    void put(char character) {
        if (size_ == sizeof(buffer_)) {
            flush();
        }
        buffer_[size_++] = character;
    }

    char buffer_[1024];
    usize size_ = 0;
};

const char* signalName(int signal) {
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV (segmentation fault)";
    case SIGBUS:
        return "SIGBUS (bus error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    case SIGFPE:
        return "SIGFPE (arithmetic exception)";
    default:
        return "unknown signal";
    }
}

const char* describeReason(int signal, const siginfo_t& info, u64 rip, const GuestCallInfo* call) {
    const u64 address = static_cast<u64>(reinterpret_cast<std::uintptr_t>(info.si_addr));

    switch (signal) {
    case SIGSEGV:
    case SIGBUS:
        if (call != nullptr && address >= call->stackBase && address < call->guardEnd) {
            return "stack overflow: the guard area of the guest stack was touched";
        }
        if (address == rip && address < 0x1000) {
            return "call or jump through a null pointer";
        }
        if (address == rip) {
            return "jump to an address that is not executable (RIP equals the fault address)";
        }
        if (address < 0x1000) {
            return "null pointer dereference (fault address below 0x1000)";
        }
        if (info.si_code == SEGV_MAPERR) {
            return "access to unmapped memory";
        }
        if (info.si_code == SEGV_ACCERR) {
            return "access refused by the memory protection (for example a write to read-only memory)";
        }
        return "memory fault";
    case SIGILL:
        return "illegal instruction";
    case SIGFPE:
        return info.si_code == FPE_INTDIV ? "integer division by zero" : "arithmetic exception";
    default:
        return "unknown";
    }
}

struct RegisterSlot {
    const char* name;
    int index;
};

constexpr RegisterSlot registerSlots[] = {
    {"rax", REG_RAX}, {"rbx", REG_RBX}, {"rcx", REG_RCX}, {"rdx", REG_RDX}, {"rsi", REG_RSI}, {"rdi", REG_RDI},
    {"rbp", REG_RBP}, {"rsp", REG_RSP}, {"r8", REG_R8},   {"r9", REG_R9},   {"r10", REG_R10}, {"r11", REG_R11},
    {"r12", REG_R12}, {"r13", REG_R13}, {"r14", REG_R14}, {"r15", REG_R15}, {"rip", REG_RIP}, {"eflags", REG_EFL},
};

void handleSignal(int signal, siginfo_t* info, void* context) {
    const auto* uc = static_cast<const ucontext_t*>(context);
    const auto& registers = uc->uc_mcontext.gregs;
    const u64 rip = static_cast<u64>(registers[REG_RIP]);
    const u64 rsp = static_cast<u64>(registers[REG_RSP]);
    const GuestCallInfo* call = currentCall;

    Writer out;
    out.text("\n=== ZenithPS5 crashed: ");
    out.text(signalName(signal));
    out.text(" ===\n");

    out.text("reason        : ");
    out.text(describeReason(signal, *info, rip, call));
    out.text("\n");

    out.text("fault address : ");
    out.hex(static_cast<u64>(reinterpret_cast<std::uintptr_t>(info->si_addr)), 16);
    out.text("\n");

    out.text("guest call    : ");
    if (call != nullptr) {
        out.text("entry ");
        out.hex(call->entry, 16);
        out.text(", stack ");
        out.hex(call->stackBase, 16);
        out.text(" - ");
        out.hex(call->stackTop, 16);
        out.text("\nrsp           : ");
        out.text(rsp >= call->stackBase && rsp <= call->stackTop ? "on the guest stack" : "NOT on the guest stack");
    } else {
        out.text("none (the fault is in the host code)");
    }
    out.text("\n");

    out.text("registers:\n");
    for (const RegisterSlot& slot : registerSlots) {
        out.text("  ");
        out.text(slot.name);
        out.padTo(8, std::strlen(slot.name));
        out.hex(static_cast<u64>(registers[slot.index]), 16);
        out.text("\n");
    }

    if (signal == SIGILL) {
        // The faulting instruction was fetched from this address, so it is readable.
        out.text("code at rip   :");
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const auto* code = reinterpret_cast<const u8*>(rip);
        for (int i = 0; i < 16; ++i) {
            out.text(" ");
            out.byte(code[i]);
        }
        out.text("\n");
    }

    out.text("=== end of report ===\n");
    out.flush();
    // SA_RESETHAND restored the default action: returning re-executes the faulting instruction, and the
    // process now dies from the original signal.
}

} // namespace

namespace detail {

const GuestCallInfo* exchangeCurrentGuestCall(const GuestCallInfo* call) {
    const GuestCallInfo* previous = currentCall;
    currentCall = call;
    return previous;
}

} // namespace detail

void installCrashHandler() {
    stack_t alternate{};
    alternate.ss_sp = alternateStack;
    alternate.ss_size = sizeof(alternateStack);
    sigaltstack(&alternate, nullptr);

    struct sigaction action {};
    action.sa_sigaction = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = static_cast<int>(SA_SIGINFO | SA_ONSTACK | static_cast<unsigned>(SA_RESETHAND));

    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
        sigaction(signal, &action, nullptr);
    }
}

} // namespace zenith::cpu
