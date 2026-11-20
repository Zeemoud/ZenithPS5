// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "cpu/guest_call.hpp"
#include "cpu/stub_table.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace zenith;

namespace {

constexpr GuestAddr codeBase = 0x2000'0000'0000ULL;
constexpr GuestAddr stubBase = 0x2000'0010'0000ULL;
constexpr GuestAddr stackBase = 0x2000'0020'0000ULL;
constexpr u64 stackSize = 256 * KiB;

// Host functions callable from the guest code.
u64 hostDouble(u64 a, u64, u64, u64, u64, u64) noexcept {
    return a * 2;
}

u64 hostWeightedSum(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) noexcept {
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e + 100000 * f;
}

void append(std::vector<u8>& code, std::initializer_list<u8> bytes) {
    code.insert(code.end(), bytes);
}

void appendU64(std::vector<u8>& code, u64 value) {
    u8 raw[sizeof(value)];
    std::memcpy(raw, &value, sizeof(value));
    code.insert(code.end(), raw, raw + sizeof(raw));
}

} // namespace

class GuestCallTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    // Maps `code` as an executable region at codeBase.
    bool loadCode(const std::vector<u8>& code) {
        if (!space.map(codeBase, memory::guestPageSize, platform::readWrite, "code")) {
            return false;
        }
        std::memcpy(memory::AddressSpace::hostPointer(codeBase), code.data(), code.size());
        return space.protect(codeBase, memory::guestPageSize, platform::readExecute);
    }

    memory::AddressSpace space;
};

TEST_F(GuestCallTest, ReturnsTheValueOfTheGuestFunction) {
    // mov eax, 42 ; ret
    ASSERT_TRUE(loadCode({0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto result = cpu::callGuest(codeBase, stack.value());

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(GuestCallTest, PassesArgumentsInSystemVRegisters) {
    // lea rax,[rdi+rsi] ; add rax,rdx ; add rax,rcx ; add rax,r8 ; add rax,r9 ; ret
    ASSERT_TRUE(loadCode(
        {0x48, 0x8D, 0x04, 0x37, 0x48, 0x01, 0xD0, 0x48, 0x01, 0xC8, 0x4C, 0x01, 0xC0, 0x4C, 0x01, 0xC8, 0xC3}));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto result = cpu::callGuest(codeBase, stack.value(), {1, 2, 3, 4, 5, 6});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 21U);
}

TEST_F(GuestCallTest, GuestSeesAStackAlignedLikeAfterACall) {
    // mov rax, rsp ; and eax, 15 ; ret
    ASSERT_TRUE(loadCode({0x48, 0x89, 0xE0, 0x83, 0xE0, 0x0F, 0xC3}));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto result = cpu::callGuest(codeBase, stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 8U);
}

TEST_F(GuestCallTest, GuestRunsOnItsOwnStack) {
    // mov rax, rsp ; ret
    ASSERT_TRUE(loadCode({0x48, 0x89, 0xE0, 0xC3}));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto result = cpu::callGuest(codeBase, stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), stack.value().top() - 8);
}

TEST_F(GuestCallTest, GuestCanCallAHostFunction) {
    auto stubs = cpu::StubTable::create(space, stubBase, 8);
    ASSERT_TRUE(stubs.ok());
    const auto stub = stubs.value().add(hostDouble);
    ASSERT_TRUE(stub.ok());

    // sub rsp, 8 ; mov edi, 20 ; movabs rax, <stub> ; call rax ; add rsp, 8 ; ret
    std::vector<u8> code;
    append(code, {0x48, 0x83, 0xEC, 0x08});
    append(code, {0xBF, 0x14, 0x00, 0x00, 0x00});
    append(code, {0x48, 0xB8});
    appendU64(code, stub.value());
    append(code, {0xFF, 0xD0});
    append(code, {0x48, 0x83, 0xC4, 0x08});
    append(code, {0xC3});
    ASSERT_TRUE(loadCode(code));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto result = cpu::callGuest(codeBase, stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 40U);
}

TEST_F(GuestCallTest, HostFunctionReceivesAllSixArguments) {
    auto stubs = cpu::StubTable::create(space, stubBase, 8);
    ASSERT_TRUE(stubs.ok());
    const auto stub = stubs.value().add(hostWeightedSum);
    ASSERT_TRUE(stub.ok());
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    // A stub is itself a valid entry point: the call goes straight guest -> host.
    const auto result = cpu::callGuest(stub.value(), stack.value(), {1, 2, 3, 4, 5, 6});

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 654321U);
}

TEST_F(GuestCallTest, RefusesToCallANonExecutableAddress) {
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto onTheStack = cpu::callGuest(stack.value().top() - 64, stack.value());
    const auto unmapped = cpu::callGuest(codeBase, stack.value());

    EXPECT_FALSE(onTheStack.ok());
    EXPECT_FALSE(unmapped.ok());
}

TEST_F(GuestCallTest, CanBeCalledRepeatedly) {
    // mov eax, 7 ; ret
    ASSERT_TRUE(loadCode({0xB8, 0x07, 0x00, 0x00, 0x00, 0xC3}));
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    for (int i = 0; i < 1000; ++i) {
        const auto result = cpu::callGuest(codeBase, stack.value());
        ASSERT_TRUE(result.ok());
        ASSERT_EQ(result.value(), 7U);
    }
}

TEST_F(GuestCallTest, TouchingTheGuardAreaFaults) {
    // movabs rax, <guard> ; mov rax, [rax] ; ret
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());
    std::vector<u8> code;
    append(code, {0x48, 0xB8});
    appendU64(code, stack.value().base());
    append(code, {0x48, 0x8B, 0x00, 0xC3});
    ASSERT_TRUE(loadCode(code));

    EXPECT_DEATH(static_cast<void>(cpu::callGuest(codeBase, stack.value())), "");
}
