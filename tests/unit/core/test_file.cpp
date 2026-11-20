// SPDX-License-Identifier: GPL-2.0-only
#include "core/file.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using namespace zenith;

namespace {

std::filesystem::path tempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("zenith_test_" + name);
}

} // namespace

TEST(ReadFile, ReadsTheWholeFile) {
    const auto path = tempPath("read_whole");
    {
        std::ofstream out(path, std::ios::binary);
        out.write("\x00\x01\x02\xFF", 4);
    }

    const auto bytes = core::readFile(path);
    std::filesystem::remove(path);

    ASSERT_TRUE(bytes.ok()) << bytes.error();
    ASSERT_EQ(bytes.value().size(), 4U);
    EXPECT_EQ(bytes.value()[0], 0x00);
    EXPECT_EQ(bytes.value()[3], 0xFF);
}

TEST(ReadFile, ReadsAnEmptyFile) {
    const auto path = tempPath("read_empty");
    {
        std::ofstream out(path, std::ios::binary);
    }

    const auto bytes = core::readFile(path);
    std::filesystem::remove(path);

    ASSERT_TRUE(bytes.ok()) << bytes.error();
    EXPECT_TRUE(bytes.value().empty());
}

TEST(ReadFile, FailsOnAMissingFile) {
    const auto bytes = core::readFile(tempPath("does_not_exist"));

    ASSERT_FALSE(bytes.ok());
    EXPECT_NE(bytes.error().find("cannot read"), std::string::npos);
}

TEST(ReadFile, FailsOnADirectory) {
    EXPECT_FALSE(core::readFile(std::filesystem::temp_directory_path()).ok());
}
