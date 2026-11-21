// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "platform/virtual_memory.hpp"

#include <algorithm>
#include <gtest/gtest.h>

using namespace zenith;

namespace {

// Far from the host executable, heap, stacks and shared libraries.
constexpr u64 baseAddress = 0x2000'0000'0000ULL;
constexpr usize reservedSize = 1 * MiB;
constexpr usize usableSize = 64 * KiB;

} // namespace

class VirtualMemoryTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }

    void TearDown() override {
        static_cast<void>(platform::release(base(), reservedSize));
        log::setLevel(log::Level::Info);
    }

    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    static u8* base() { return reinterpret_cast<u8*>(baseAddress); }
};

TEST_F(VirtualMemoryTest, PageSizeIsAPowerOfTwo) {
    const usize size = platform::pageSize();
    EXPECT_GE(size, 4096U);
    EXPECT_EQ(size & (size - 1), 0U);
}

TEST_F(VirtualMemoryTest, ReservedMemoryIsZeroedAndUsableAfterProtect) {
    ASSERT_TRUE(platform::reserveFixed(base(), reservedSize));
    ASSERT_TRUE(platform::protect(base(), usableSize, platform::readWrite));

    EXPECT_TRUE(std::all_of(base(), base() + usableSize, [](u8 byte) { return byte == 0; }));

    std::fill(base(), base() + usableSize, static_cast<u8>(0xAB));
    EXPECT_TRUE(std::all_of(base(), base() + usableSize, [](u8 byte) { return byte == 0xAB; }));
}

TEST_F(VirtualMemoryTest, ReserveFixedRefusesAnOccupiedRange) {
    ASSERT_TRUE(platform::reserveFixed(base(), reservedSize));

    EXPECT_FALSE(platform::reserveFixed(base(), reservedSize));
    EXPECT_FALSE(platform::reserveFixed(base() + platform::pageSize(), platform::pageSize()));
}

TEST_F(VirtualMemoryTest, DecommitZeroesTheMemory) {
    ASSERT_TRUE(platform::reserveFixed(base(), reservedSize));
    ASSERT_TRUE(platform::protect(base(), usableSize, platform::readWrite));
    std::fill(base(), base() + usableSize, static_cast<u8>(0xFF));

    ASSERT_TRUE(platform::decommit(base(), usableSize));
    ASSERT_TRUE(platform::protect(base(), usableSize, platform::readWrite));

    EXPECT_TRUE(std::all_of(base(), base() + usableSize, [](u8 byte) { return byte == 0; }));
}

TEST_F(VirtualMemoryTest, ReleaseAllowsReservingAgain) {
    ASSERT_TRUE(platform::reserveFixed(base(), reservedSize));
    ASSERT_TRUE(platform::release(base(), reservedSize));

    EXPECT_TRUE(platform::reserveFixed(base(), reservedSize));
}

TEST_F(VirtualMemoryTest, ReadOnlyMemoryCannotBeWritten) {
    ASSERT_TRUE(platform::reserveFixed(base(), reservedSize));
    ASSERT_TRUE(platform::protect(base(), usableSize, platform::readWrite));
    *base() = 1;
    ASSERT_TRUE(platform::protect(base(), usableSize, platform::Protection::Read));

    EXPECT_EQ(*base(), 1);
    EXPECT_DEATH(*static_cast<volatile u8*>(base()) = 2, "");
}
