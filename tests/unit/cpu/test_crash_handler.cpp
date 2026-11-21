// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "cpu/crash_handler.hpp"
#include "cpu/guest_call.hpp"

#include <csignal>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace zenith;
using ::testing::KilledBySignal;

namespace {

constexpr GuestAddr codeBase = 0x2000'0000'0000ULL;
constexpr GuestAddr stackBase = 0x2000'0020'0000ULL;
constexpr u64 stackSize = 256 * KiB;

void appendU64(std::vector<u8>& code, u64 value) {
    u8 raw[sizeof(value)];
    std::memcpy(raw, &value, sizeof(value));
    code.insert(code.end(), raw, raw + sizeof(raw));
}

} // namespace

// Each test crashes a guest program inside a death test, with the crash handler installed in the child
// process, and checks the report printed on stderr and the signal the process dies from.
class CrashHandlerTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    // Maps `code` at codeBase, runs it on a fresh stack with the crash handler installed.
    void runCrashing(const std::vector<u8>& code) {
        cpu::installCrashHandler();
        if (!space.map(codeBase, memory::guestPageSize, platform::readWrite, "code")) {
            return;
        }
        std::memcpy(memory::AddressSpace::hostPointer(codeBase), code.data(), code.size());
        if (!space.protect(codeBase, memory::guestPageSize, platform::readExecute)) {
            return;
        }
        auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
        if (!stack.ok()) {
            return;
        }
        static_cast<void>(cpu::callGuest(codeBase, stack.value()));
    }

    memory::AddressSpace space;
};

TEST_F(CrashHandlerTest, ReportsANullPointerDereference) {
    // xor eax, eax ; mov rax, [rax] ; ret
    EXPECT_EXIT(runCrashing({0x31, 0xC0, 0x48, 0x8B, 0x00, 0xC3}), KilledBySignal(SIGSEGV), "null pointer dereference");
}

TEST_F(CrashHandlerTest, ReportsAStackOverflow) {
    // loop: push rax ; jmp loop
    EXPECT_EXIT(runCrashing({0x50, 0xEB, 0xFD}), KilledBySignal(SIGSEGV), "stack overflow");
}

TEST_F(CrashHandlerTest, ReportsAnIllegalInstructionWithItsBytes) {
    // ud2
    EXPECT_EXIT(runCrashing({0x0F, 0x0B}), KilledBySignal(SIGILL), "illegal instruction[^$]*code at rip +: 0f 0b");
}

TEST_F(CrashHandlerTest, ReportsADivisionByZero) {
    // xor edx, edx ; xor ecx, ecx ; mov eax, 1 ; div ecx ; ret
    EXPECT_EXIT(runCrashing({0x31, 0xD2, 0x31, 0xC9, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xF7, 0xF1, 0xC3}),
                KilledBySignal(SIGFPE), "integer division by zero");
}

TEST_F(CrashHandlerTest, ReportsAJumpToNonExecutableMemory) {
    // movabs rax, <the stack, which is not executable> ; jmp rax
    std::vector<u8> code = {0x48, 0xB8};
    appendU64(code, stackBase + stackSize - 64);
    code.insert(code.end(), {0xFF, 0xE0});

    EXPECT_EXIT(runCrashing(code), KilledBySignal(SIGSEGV), "not executable");
}

TEST_F(CrashHandlerTest, ReportsAWriteToReadOnlyMemory) {
    // movabs rax, <the code page, read-only after loading> ; mov [rax], al ; ret
    std::vector<u8> code = {0x48, 0xB8};
    appendU64(code, codeBase);
    code.insert(code.end(), {0x88, 0x00, 0xC3});

    EXPECT_EXIT(runCrashing(code), KilledBySignal(SIGSEGV), "memory protection");
}

TEST_F(CrashHandlerTest, ReportsTheGuestCallAndTheRegisters) {
    // mov eax, 0x1234 ; xor ecx, ecx ; mov rcx, [rcx]
    EXPECT_EXIT(runCrashing({0xB8, 0x34, 0x12, 0x00, 0x00, 0x31, 0xC9, 0x48, 0x8B, 0x09}), KilledBySignal(SIGSEGV),
                "guest call +: entry 0x0000200000000000[^$]*on the guest stack[^$]*rax +0x0000000000001234");
}

TEST_F(CrashHandlerTest, ReportsACrashOutsideOfAnyGuestCall) {
    EXPECT_EXIT(
        {
            cpu::installCrashHandler();
            int* volatile pointer = nullptr; // volatile: the compiler cannot prove it is null
            *pointer = 1;
        },
        KilledBySignal(SIGSEGV), "the fault is in the host code");
}
