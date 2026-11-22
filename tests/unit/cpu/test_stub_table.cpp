// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "cpu/stub_table.hpp"

#include <gtest/gtest.h>

using namespace zenith;

namespace {

constexpr GuestAddr stubBase = 0x2000'0010'0000ULL;

u64 hostNoop(u64, u64, u64, u64, u64, u64) noexcept {
    return 0;
}

} // namespace

class StubTableTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    memory::AddressSpace space;
};

TEST_F(StubTableTest, HandsOutConsecutiveStubs) {
    auto table = cpu::StubTable::create(space, stubBase, 8);
    ASSERT_TRUE(table.ok()) << table.error();

    const auto first = table.value().add(hostNoop);
    const auto second = table.value().add(hostNoop);

    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.value(), stubBase);
    EXPECT_EQ(second.value(), stubBase + cpu::StubTable::stubSize);
    EXPECT_EQ(table.value().size(), 2U);
}

TEST_F(StubTableTest, StubsLiveInExecutableMemory) {
    auto table = cpu::StubTable::create(space, stubBase, 8);
    ASSERT_TRUE(table.ok());
    ASSERT_TRUE(table.value().add(hostNoop).ok());

    const auto region = space.find(stubBase);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->protection, platform::readExecute);
}

TEST_F(StubTableTest, RefusesToGrowBeyondItsCapacity) {
    auto table = cpu::StubTable::create(space, stubBase, 2);
    ASSERT_TRUE(table.ok());
    ASSERT_TRUE(table.value().add(hostNoop).ok());
    ASSERT_TRUE(table.value().add(hostNoop).ok());

    const auto third = table.value().add(hostNoop);

    ASSERT_FALSE(third.ok());
    EXPECT_NE(third.error().find("full"), std::string::npos);
}

TEST_F(StubTableTest, RejectsAZeroCapacity) {
    EXPECT_FALSE(cpu::StubTable::create(space, stubBase, 0).ok());
}

TEST_F(StubTableTest, ReleasesItsMemoryWhenDestroyed) {
    {
        auto table = cpu::StubTable::create(space, stubBase, 8);
        ASSERT_TRUE(table.ok());
    }

    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(StubTableTest, SlotSizeSpacesTheStubs) {
    auto table = cpu::StubTable::create(space, stubBase, 8, cpu::StubTable::idStubSize);
    ASSERT_TRUE(table.ok()) << table.error();

    const auto first = table.value().add(hostNoop);
    const auto second = table.value().add(hostNoop);

    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second.value() - first.value(), cpu::StubTable::idStubSize);
    EXPECT_EQ(table.value().slotSize(), cpu::StubTable::idStubSize);
}

TEST_F(StubTableTest, RejectsABadSlotSize) {
    EXPECT_FALSE(cpu::StubTable::create(space, stubBase, 8, 8).ok());
    EXPECT_FALSE(cpu::StubTable::create(space, stubBase, 8, 24).ok());
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(StubTableTest, StubsWithAnIdNeedBigSlots) {
    auto table = cpu::StubTable::create(space, stubBase, 8);
    ASSERT_TRUE(table.ok());

    EXPECT_FALSE(table.value().addWithId(hostNoop, 1).ok());
}
