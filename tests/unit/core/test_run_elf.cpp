// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/run_elf.hpp"
#include "elf_builder.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace zenith;
using test::ElfBuilder;

namespace {

constexpr GuestAddr codeBase = 0x2000'0000'0000ULL;

// mov eax, 42 ; ret
const std::vector<u8> returns42 = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};

std::vector<u8> program(const std::vector<u8>& code) {
    return ElfBuilder{}.entry(codeBase).segment({.vaddr = codeBase, .bytes = code}).build();
}

} // namespace

class RunElfTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }
};

TEST_F(RunElfTest, RunsAProgramAndReturnsItsResult) {
    const auto result = core::runElf(program(returns42), "test.elf");

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(RunElfTest, ReleasesEverythingSoItCanRunAgain) {
    const auto bytes = program(returns42);

    for (int i = 0; i < 3; ++i) {
        const auto result = core::runElf(bytes, "test.elf");
        ASSERT_TRUE(result.ok()) << result.error();
        ASSERT_EQ(result.value(), 42U);
    }
}

TEST_F(RunElfTest, ReportsAnInvalidFileWithItsName) {
    const std::vector<u8> garbage(100, 0xAB);

    const auto result = core::runElf(garbage, "garbage.bin");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("garbage.bin"), std::string::npos);
}

TEST_F(RunElfTest, ReportsALoadFailure) {
    // The entry point is outside the only segment.
    const auto bytes = ElfBuilder{}.entry(codeBase + 0x100000).segment({.vaddr = codeBase, .bytes = returns42}).build();

    const auto result = core::runElf(bytes, "bad_entry.elf");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("entry point"), std::string::npos);
}

TEST_F(RunElfTest, HonorsTheStackSizeOption) {
    core::RunOptions options;
    options.stackSize = 0; // too small to hold even the guard area

    const auto result = core::runElf(program(returns42), "test.elf", options);

    EXPECT_FALSE(result.ok());
}
