// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/bridge.hpp"
#include "core/run_elf.hpp"
#include "elf_builder.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

using namespace zenith;
using core::BridgeFunction;
using test::ElfBuilder;

namespace {

constexpr GuestAddr codeBase = 0x2000'0000'0000ULL;
constexpr GuestAddr dataBase = codeBase + 0x10000;
const std::string message = "Hello from the guest!\n";

// Assembles a guest program that calls bridge functions with up to three arguments.
class Code {
public:
    Code& prologue() { // sub rsp, 8: keeps the stack aligned for calls
        append({0x48, 0x83, 0xEC, 0x08});
        return *this;
    }

    Code& epilogue() { // add rsp, 8 ; ret
        append({0x48, 0x83, 0xC4, 0x08, 0xC3});
        return *this;
    }

    Code& call(BridgeFunction function, u64 a = 0, u64 b = 0, u64 c = 0) {
        loadRegister(0xBF, a);                             // movabs rdi
        loadRegister(0xBE, b);                             // movabs rsi
        loadRegister(0xBA, c);                             // movabs rdx
        loadRegister(0xB8, core::bridgeAddress(function)); // movabs rax
        append({0xFF, 0xD0});                              // call rax
        return *this;
    }

    Code& moveEax(u32 value) { // mov eax, imm32
        bytes_.push_back(0xB8);
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<u8>(value >> (8 * i)));
        }
        return *this;
    }

    const std::vector<u8>& bytes() const { return bytes_; }

private:
    void append(std::initializer_list<u8> values) { bytes_.insert(bytes_.end(), values); }

    void loadRegister(u8 opcode, u64 value) {
        bytes_.push_back(0x48);
        bytes_.push_back(opcode);
        for (int i = 0; i < 8; ++i) {
            bytes_.push_back(static_cast<u8>(value >> (8 * i)));
        }
    }

    std::vector<u8> bytes_;
};

struct Run {
    Result<u64> result = makeError("not run");
    std::vector<std::pair<int, std::string>> writes;
};

Run runProgram(const Code& code) {
    const auto bytes = ElfBuilder{}
                           .entry(codeBase)
                           .segment({.vaddr = codeBase, .bytes = code.bytes()})
                           .segment({.flags = loader::elf::flagRead,
                                     .vaddr = dataBase,
                                     .bytes = std::vector<u8>(message.begin(), message.end())})
                           .build();

    Run run;
    core::RunOptions options;
    options.output = [&run](int fd, std::string_view text) { run.writes.emplace_back(fd, std::string(text)); };
    run.result = core::runElf(bytes, "bridge_test.elf", options);
    return run;
}

} // namespace

class BridgeTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }
};

TEST_F(BridgeTest, FunctionsLiveAtFixedAddresses) {
    EXPECT_EQ(core::bridgeAddress(BridgeFunction::Write), 0x100000U);
    EXPECT_EQ(core::bridgeAddress(BridgeFunction::Exit), 0x100010U);
}

TEST_F(BridgeTest, HelloFromTheGuest) {
    const auto run = runProgram(Code{}
                                    .prologue()
                                    .call(BridgeFunction::Write, 1, dataBase, message.size())
                                    .call(BridgeFunction::Exit, 7)
                                    .epilogue());

    ASSERT_TRUE(run.result.ok()) << run.result.error();
    EXPECT_EQ(run.result.value(), 7U);
    ASSERT_EQ(run.writes.size(), 1U);
    EXPECT_EQ(run.writes[0].first, 1);
    EXPECT_EQ(run.writes[0].second, message);
}

TEST_F(BridgeTest, WriteReturnsTheByteCount) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Write, 1, dataBase, 5).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), 5U);
    ASSERT_EQ(run.writes.size(), 1U);
    EXPECT_EQ(run.writes[0].second, "Hello");
}

TEST_F(BridgeTest, WriteCanTargetStandardError) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Write, 2, dataBase, message.size()).epilogue());

    ASSERT_TRUE(run.result.ok());
    ASSERT_EQ(run.writes.size(), 1U);
    EXPECT_EQ(run.writes[0].first, 2);
}

TEST_F(BridgeTest, WriteRejectsAnUnknownFileDescriptor) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Write, 5, dataBase, 3).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), ~0ULL);
    EXPECT_TRUE(run.writes.empty());
}

TEST_F(BridgeTest, WriteRejectsMemoryThatIsNotMapped) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Write, 1, 0x1234'0000'0000ULL, 4).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), ~0ULL);
    EXPECT_TRUE(run.writes.empty());
}

TEST_F(BridgeTest, WriteRejectsAnExcessiveLength) {
    const auto run =
        runProgram(Code{}.prologue().call(BridgeFunction::Write, 1, dataBase, core::maxBridgeWriteSize + 1).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), ~0ULL);
    EXPECT_TRUE(run.writes.empty());
}

TEST_F(BridgeTest, AZeroLengthWriteDoesNothing) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Write, 1, dataBase, 0).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), 0U);
    EXPECT_TRUE(run.writes.empty());
}

TEST_F(BridgeTest, ExitStopsTheProgram) {
    const auto run = runProgram(Code{}.prologue().call(BridgeFunction::Exit, 3).moveEax(1).epilogue());

    ASSERT_TRUE(run.result.ok());
    EXPECT_EQ(run.result.value(), 3U);
}

TEST_F(BridgeTest, ProgramsCannotClaimTheBridgeAddresses) {
    const auto bytes =
        ElfBuilder{}.entry(core::bridgeBase).segment({.vaddr = core::bridgeBase, .bytes = {0xC3}}).build();

    const auto result = core::runElf(bytes, "squatter.elf");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("cannot map"), std::string::npos);
}
