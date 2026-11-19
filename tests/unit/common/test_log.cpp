// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

using namespace zenith;

class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        lines_.clear();
        log::setLevel(log::Level::Info);
        log::setSink([this](log::Level, log::Channel, std::string_view line) { lines_.emplace_back(line); });
    }

    void TearDown() override {
        log::setSink({});
        log::setLevel(log::Level::Info);
    }

    static bool contains(const std::string& text, std::string_view part) {
        return text.find(part) != std::string::npos;
    }

    std::vector<std::string> lines_;
};

TEST_F(LogTest, WritesFormattedMessage) {
    ZENITH_LOG_INFO(Kernel, "hello {}", 42);

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_TRUE(contains(lines_[0], "hello 42"));
    EXPECT_TRUE(contains(lines_[0], "[Kernel]"));
    EXPECT_TRUE(contains(lines_[0], "[I]"));
}

TEST_F(LogTest, FiltersMessagesBelowTheLevel) {
    ZENITH_LOG_DEBUG(Kernel, "hidden");

    EXPECT_TRUE(lines_.empty());
}

TEST_F(LogTest, LevelCanBeSetPerChannel) {
    log::setLevel(log::Channel::Gpu, log::Level::Trace);

    ZENITH_LOG_TRACE(Gpu, "visible");
    ZENITH_LOG_TRACE(Kernel, "hidden");

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_TRUE(contains(lines_[0], "visible"));
}

TEST_F(LogTest, OffSilencesEverything) {
    log::setLevel(log::Level::Off);

    ZENITH_LOG_FATAL(Kernel, "hidden");

    EXPECT_TRUE(lines_.empty());
}

TEST_F(LogTest, DoesNotEvaluateArgumentsOfFilteredMessages) {
    int calls = 0;
    auto expensive = [&calls] {
        ++calls;
        return 1;
    };

    ZENITH_LOG_TRACE(Kernel, "{}", expensive());

    EXPECT_EQ(calls, 0);
}

TEST_F(LogTest, ChannelNames) {
    EXPECT_EQ(log::toString(log::Channel::Loader), "Loader");
    EXPECT_EQ(log::toString(log::Channel::Frontend), "Frontend");
}
