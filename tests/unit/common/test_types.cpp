// SPDX-License-Identifier: GPL-2.0-only
#include "common/types.hpp"

#include <gtest/gtest.h>

using namespace zenith;

TEST(Types, FixedWidths) {
    EXPECT_EQ(sizeof(u8), 1U);
    EXPECT_EQ(sizeof(u16), 2U);
    EXPECT_EQ(sizeof(u32), 4U);
    EXPECT_EQ(sizeof(u64), 8U);
    EXPECT_EQ(sizeof(i8), 1U);
    EXPECT_EQ(sizeof(i16), 2U);
    EXPECT_EQ(sizeof(i32), 4U);
    EXPECT_EQ(sizeof(i64), 8U);
    EXPECT_EQ(sizeof(GuestAddr), 8U);
}

TEST(Types, SizeUnits) {
    EXPECT_EQ(KiB, 1024ULL);
    EXPECT_EQ(MiB, 1024ULL * 1024ULL);
    EXPECT_EQ(GiB, 1024ULL * 1024ULL * 1024ULL);
}
