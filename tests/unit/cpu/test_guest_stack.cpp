// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "cpu/guest_stack.hpp"

#include <gtest/gtest.h>

using namespace zenith;

namespace {

constexpr GuestAddr stackBase = 0x2000'0000'0000ULL;
constexpr u64 stackSize = 256 * KiB;

} // namespace

class GuestStackTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    memory::AddressSpace space;
};

TEST_F(GuestStackTest, MapsAGuardAreaAndAWritableStack) {
    auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "test");

    ASSERT_TRUE(stack.ok()) << stack.error();
    EXPECT_EQ(stack.value().base(), stackBase);
    EXPECT_EQ(stack.value().top(), stackBase + stackSize);
    EXPECT_EQ(stack.value().top() % 16, 0U);

    const auto guard = space.find(stackBase);
    ASSERT_TRUE(guard.has_value());
    EXPECT_EQ(guard->protection, platform::Protection::None);
    EXPECT_EQ(guard->size, cpu::stackGuardSize);

    const auto usable = space.find(stack.value().top() - 1);
    ASSERT_TRUE(usable.has_value());
    EXPECT_EQ(usable->protection, platform::readWrite);
}

TEST_F(GuestStackTest, ReleasesItsMemoryWhenDestroyed) {
    {
        auto stack = cpu::GuestStack::create(space, stackBase, stackSize, "test");
        ASSERT_TRUE(stack.ok());
        EXPECT_EQ(space.regionCount(), 2U);
    }

    EXPECT_EQ(space.regionCount(), 0U);
    EXPECT_TRUE(cpu::GuestStack::create(space, stackBase, stackSize, "again").ok());
}

TEST_F(GuestStackTest, MovingTransfersOwnership) {
    auto first = cpu::GuestStack::create(space, stackBase, stackSize, "test");
    ASSERT_TRUE(first.ok());

    {
        cpu::GuestStack moved = std::move(first).value();
        EXPECT_EQ(moved.base(), stackBase);
        EXPECT_EQ(space.regionCount(), 2U);
    }

    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(GuestStackTest, RejectsASizeThatLeavesNoUsableMemory) {
    EXPECT_FALSE(cpu::GuestStack::create(space, stackBase, cpu::stackGuardSize, "tiny").ok());
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(GuestStackTest, FailsCleanlyWhenTheRangeIsTaken) {
    ASSERT_TRUE(space.map(stackBase + cpu::stackGuardSize, memory::guestPageSize, platform::readWrite, "blocker"));

    EXPECT_FALSE(cpu::GuestStack::create(space, stackBase, stackSize, "blocked").ok());

    EXPECT_EQ(space.regionCount(), 1U); // only the blocker: the guard area was rolled back
}
