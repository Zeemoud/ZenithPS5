// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "memory/address_space.hpp"

#include <algorithm>
#include <gtest/gtest.h>

using namespace zenith;
using memory::guestPageSize;

namespace {

constexpr GuestAddr base = 0x2000'0000'0000ULL;
constexpr u64 oneRegion = 4 * guestPageSize;

int hostVariable = 0; // lives in the host's own data segment: certainly mapped

} // namespace

class AddressSpaceTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    memory::AddressSpace space;
};

TEST_F(AddressSpaceTest, MappedMemoryIsZeroedAndWritable) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "test"));

    u8* bytes = memory::AddressSpace::hostPointer(base);
    EXPECT_TRUE(std::all_of(bytes, bytes + oneRegion, [](u8 byte) { return byte == 0; }));
    std::fill(bytes, bytes + oneRegion, static_cast<u8>(0x5A));
    EXPECT_EQ(bytes[oneRegion - 1], 0x5A);
}

TEST_F(AddressSpaceTest, FindReturnsTheRegionContainingAnAddress) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "heap"));

    const auto region = space.find(base + oneRegion - 1);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->name, "heap");
    EXPECT_EQ(region->start, base);
    EXPECT_EQ(region->size, oneRegion);
    EXPECT_FALSE(space.find(base + oneRegion).has_value());
    EXPECT_FALSE(space.find(base - 1).has_value());
}

TEST_F(AddressSpaceTest, RejectsInvalidRanges) {
    EXPECT_FALSE(space.map(base + 1, oneRegion, platform::readWrite, "unaligned address"));
    EXPECT_FALSE(space.map(base, oneRegion + 1, platform::readWrite, "unaligned size"));
    EXPECT_FALSE(space.map(base, 0, platform::readWrite, "empty"));
    EXPECT_FALSE(space.map(~0ULL - guestPageSize + 1, guestPageSize, platform::readWrite, "outside user space"));
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(AddressSpaceTest, RejectsOverlappingRegions) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "first"));

    EXPECT_FALSE(space.map(base, oneRegion, platform::readWrite, "same"));
    EXPECT_FALSE(space.map(base + guestPageSize, guestPageSize, platform::readWrite, "inside"));
    EXPECT_FALSE(space.map(base - guestPageSize, 2 * guestPageSize, platform::readWrite, "overlaps start"));
    EXPECT_FALSE(space.map(base + oneRegion - guestPageSize, 2 * guestPageSize, platform::readWrite, "overlaps end"));
    EXPECT_EQ(space.regionCount(), 1U);
}

TEST_F(AddressSpaceTest, AcceptsAdjacentRegions) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "first"));

    EXPECT_TRUE(space.map(base + oneRegion, oneRegion, platform::readWrite, "second"));
    EXPECT_TRUE(space.map(base - guestPageSize, guestPageSize, platform::readWrite, "before"));
    EXPECT_EQ(space.regionCount(), 3U);
}

TEST_F(AddressSpaceTest, NeverReplacesAMappingOwnedByTheHost) {
    const GuestAddr hostPage = memory::AddressSpace::guestAddress(&hostVariable) & ~(guestPageSize - 1);

    EXPECT_FALSE(space.map(hostPage, guestPageSize, platform::readWrite, "clobber"));

    hostVariable = 42; // would crash if the host mapping had been replaced
    EXPECT_EQ(hostVariable, 42);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(AddressSpaceTest, UnmapRequiresTheExactRegion) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "region"));

    EXPECT_FALSE(space.unmap(base, guestPageSize));
    EXPECT_FALSE(space.unmap(base + guestPageSize, guestPageSize));
    EXPECT_TRUE(space.unmap(base, oneRegion));
    EXPECT_FALSE(space.find(base).has_value());
    EXPECT_TRUE(space.map(base, oneRegion, platform::readWrite, "again"));
}

TEST_F(AddressSpaceTest, ProtectUpdatesTheRegion) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "region"));

    EXPECT_FALSE(space.protect(base, guestPageSize, platform::Protection::Read));
    ASSERT_TRUE(space.protect(base, oneRegion, platform::Protection::Read));
    EXPECT_EQ(space.find(base)->protection, platform::Protection::Read);
}

TEST_F(AddressSpaceTest, IsMappedSpansAdjacentRegionsButNotHoles) {
    ASSERT_TRUE(space.map(base, oneRegion, platform::readWrite, "a"));
    ASSERT_TRUE(space.map(base + oneRegion, oneRegion, platform::readWrite, "b"));
    ASSERT_TRUE(space.map(base + 3 * oneRegion, oneRegion, platform::readWrite, "c"));

    EXPECT_TRUE(space.isMapped(base, oneRegion));
    EXPECT_TRUE(space.isMapped(base + oneRegion - 8, 16));
    EXPECT_TRUE(space.isMapped(base, 2 * oneRegion));
    EXPECT_FALSE(space.isMapped(base, 3 * oneRegion));
    EXPECT_FALSE(space.isMapped(base + 2 * oneRegion, 1));
    EXPECT_FALSE(space.isMapped(base, 0));
}

TEST_F(AddressSpaceTest, DestructorGivesTheMemoryBack) {
    {
        memory::AddressSpace scoped;
        ASSERT_TRUE(scoped.map(base, oneRegion, platform::readWrite, "temporary"));
    }

    EXPECT_TRUE(space.map(base, oneRegion, platform::readWrite, "reused"));
}
