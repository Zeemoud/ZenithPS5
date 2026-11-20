// SPDX-License-Identifier: GPL-2.0-only
#include "common/result.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace zenith;

namespace {

Result<int> parsePositive(int input) {
    if (input <= 0) {
        return makeError("{} is not positive", input);
    }
    return input * 2;
}

} // namespace

TEST(Result, HoldsAValue) {
    const auto result = parsePositive(21);

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
}

TEST(Result, HoldsAnErrorWithFormattedMessage) {
    const auto result = parsePositive(-3);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error(), "-3 is not positive");
}

TEST(Result, SupportsMoveOnlyValues) {
    Result<std::unique_ptr<int>> result = std::make_unique<int>(7);

    ASSERT_TRUE(result.ok());
    const std::unique_ptr<int> taken = std::move(result).value();
    EXPECT_EQ(*taken, 7);
}

TEST(ResultDeathTest, AccessingTheValueOfAnErrorAborts) {
    const auto result = parsePositive(0);

    EXPECT_DEATH(static_cast<void>(result.value()), "0 is not positive");
}

TEST(ResultDeathTest, AccessingTheErrorOfAValueAborts) {
    const auto result = parsePositive(1);

    EXPECT_DEATH(static_cast<void>(result.error()), "holds a value");
}
