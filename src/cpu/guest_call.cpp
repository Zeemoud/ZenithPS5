// SPDX-License-Identifier: GPL-2.0-only
#include "cpu/guest_call.hpp"

#include "common/log.hpp"

#include <cstddef>

// Layout shared with the assembly below.
struct ZenithEnterContext {
    zenith::u64 entry;
    zenith::u64 stackTop;
    zenith::u64 args[6];
};
static_assert(sizeof(ZenithEnterContext) == 64);
static_assert(offsetof(ZenithEnterContext, stackTop) == 8);
static_assert(offsetof(ZenithEnterContext, args) == 16);

extern "C" zenith::u64 zenith_cpu_enter(const ZenithEnterContext* context);

// zenith_cpu_enter(context): switches to the guest stack, loads the argument registers, calls the guest
// entry point and switches back. The host's callee-saved registers are saved and restored; the host stack
// pointer is kept in r12, which a System V conforming guest function preserves.
// `call` pushes the return address, so the guest sees RSP = top - 8, exactly as after a normal call.
// clang-format off
asm(R"(
    .text
    .p2align 4
    .globl zenith_cpu_enter
    .type zenith_cpu_enter, @function
zenith_cpu_enter:
    .intel_syntax noprefix
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    mov r13, rdi
    mov rax, [r13 + 0]
    mov rsp, [r13 + 8]
    mov rdi, [r13 + 16]
    mov rsi, [r13 + 24]
    mov rdx, [r13 + 32]
    mov rcx, [r13 + 40]
    mov r8,  [r13 + 48]
    mov r9,  [r13 + 56]
    call rax
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
    .att_syntax prefix
    .size zenith_cpu_enter, .-zenith_cpu_enter
)");
// clang-format on

namespace zenith::cpu {

Result<u64> callGuest(GuestAddr entry, const GuestStack& stack, const GuestArgs& args) {
    const auto region = stack.space().find(entry);
    if (!region.has_value() || !platform::hasFlag(region->protection, platform::Protection::Execute)) {
        return makeError("cannot call {:#x}: not inside an executable region", entry);
    }

    ZenithEnterContext context{};
    context.entry = entry;
    context.stackTop = stack.top();
    for (usize i = 0; i < args.size(); ++i) {
        context.args[i] = args[i];
    }

    ZENITH_LOG_TRACE(Cpu, "entering guest code at {:#x}", entry);
    const u64 result = zenith_cpu_enter(&context);
    ZENITH_LOG_TRACE(Cpu, "guest code returned {:#x}", result);
    return result;
}

} // namespace zenith::cpu
