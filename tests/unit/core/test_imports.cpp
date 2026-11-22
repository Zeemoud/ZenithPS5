// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/imports.hpp"
#include "cpu/guest_call.hpp"

#include <gtest/gtest.h>

using namespace zenith;

namespace {

constexpr GuestAddr importsBase = 0x2000'0010'0000ULL;
constexpr GuestAddr stackBase = 0x2000'0020'0000ULL;
constexpr u64 stackSize = 256 * KiB;

u64 hostDouble(u64 a, u64, u64, u64, u64, u64) noexcept {
    return a * 2;
}

u64 hostTriple(u64 a, u64, u64, u64, u64, u64) noexcept {
    return a * 3;
}

} // namespace

class ImportTableTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    memory::AddressSpace space;
};

TEST_F(ImportTableTest, ResolvesAFunctionToAStubThatCallsIt) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok()) << imports.error();
    imports.value().addFunction("libtest.so", "double_it", hostDouble);
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "stack");
    ASSERT_TRUE(stack.ok());

    const auto stub = imports.value().resolve("double_it");
    ASSERT_TRUE(stub.ok()) << stub.error();
    const auto result = cpu::callGuest(stub.value(), stack.value(), {21});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(ImportTableTest, ASymbolAlwaysGetsTheSameStub) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    imports.value().addFunction("libtest.so", "double_it", hostDouble);
    imports.value().addFunction("libtest.so", "triple_it", hostTriple);

    const auto first = imports.value().resolve("double_it");
    const auto again = imports.value().resolve("double_it");
    const auto other = imports.value().resolve("triple_it");

    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(again.ok());
    ASSERT_TRUE(other.ok());
    EXPECT_EQ(first.value(), again.value());
    EXPECT_NE(first.value(), other.value());
}

TEST_F(ImportTableTest, SymbolsAreFoundWhicheverLibraryHoldsThem) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    imports.value().addFunction("liba.so", "double_it", hostDouble);
    imports.value().addFunction("libb.so", "triple_it", hostTriple);

    EXPECT_TRUE(imports.value().resolve("double_it").ok());
    EXPECT_TRUE(imports.value().resolve("triple_it").ok());
    EXPECT_TRUE(imports.value().unimplementedNames().empty());
}

TEST_F(ImportTableTest, UnknownSymbolsGetAStubAndAreListed) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());

    const auto first = imports.value().resolve("not_there");
    const auto second = imports.value().resolve("neither");
    const auto again = imports.value().resolve("not_there");

    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    ASSERT_TRUE(again.ok());
    EXPECT_NE(first.value(), second.value());
    EXPECT_EQ(first.value(), again.value());
    ASSERT_EQ(imports.value().unimplementedNames().size(), 2U);
    EXPECT_EQ(imports.value().unimplementedName(0), "not_there");
    EXPECT_EQ(imports.value().unimplementedName(1), "neither");
}

TEST_F(ImportTableTest, KnowsTheLibrariesItProvides) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    imports.value().addFunction("libtest.so", "double_it", hostDouble);
    imports.value().addLibrary("libempty.so");

    EXPECT_TRUE(imports.value().hasLibrary("libtest.so"));
    EXPECT_TRUE(imports.value().hasLibrary("libempty.so"));
    EXPECT_FALSE(imports.value().hasLibrary("libother.so"));
}

TEST_F(ImportTableTest, ReleasesItsMemoryWhenDestroyed) {
    {
        auto imports = core::ImportTable::create(space, importsBase);
        ASSERT_TRUE(imports.ok());
    }

    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ImportTableTest, FailsCleanlyWhenTheStubTableIsFull) {
    auto imports = core::ImportTable::create(space, importsBase, 2);
    ASSERT_TRUE(imports.ok());

    EXPECT_TRUE(imports.value().resolve("one").ok());
    EXPECT_TRUE(imports.value().resolve("two").ok());
    const auto third = imports.value().resolve("three");

    ASSERT_FALSE(third.ok());
    EXPECT_NE(third.error().find("three"), std::string::npos);
}
