// SPDX-License-Identifier: GPL-2.0-only
#include "common/assert.hpp"

#include <gtest/gtest.h>

TEST(Assert, VerifyPassesSilentlyWhenTrue) {
    ZENITH_VERIFY(1 + 1 == 2);
    ZENITH_VERIFY(true, "never shown {}", 1);
    SUCCEED();
}

TEST(AssertDeathTest, VerifyAbortsAndReportsTheMessage) {
    EXPECT_DEATH(ZENITH_VERIFY(false, "boom {}", 7), "boom 7");
}

TEST(AssertDeathTest, VerifyReportsTheExpression) {
    EXPECT_DEATH(ZENITH_VERIFY(1 == 2), "1 == 2");
}

TEST(AssertDeathTest, UnreachableAborts) {
    EXPECT_DEATH(ZENITH_UNREACHABLE("opcode {:#x}", 0xff), "opcode 0xff");
}

TEST(AssertDeathTest, UnimplementedAborts) {
    EXPECT_DEATH(ZENITH_UNIMPLEMENTED("sceKernelFoo"), "sceKernelFoo");
}

#ifdef NDEBUG
TEST(Assert, AssertIsCompiledOutInRelease) {
    ZENITH_ASSERT(false, "not evaluated in release");
    SUCCEED();
}
#else
TEST(AssertDeathTest, AssertAbortsInDebug) {
    EXPECT_DEATH(ZENITH_ASSERT(false, "debug check"), "debug check");
}
#endif
