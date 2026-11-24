// SPDX-License-Identifier: GPL-2.0-only
#include "core/library_source.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using namespace zenith;

namespace {

// A fresh temporary directory, removed at the end of the test.
class TempDirectory {
public:
    explicit TempDirectory(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / ("zenith_test_" + name)) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

    void write(const std::string& fileName, const std::string& content) const {
        std::ofstream out(path_ / fileName, std::ios::binary);
        out << content;
    }

private:
    std::filesystem::path path_;
};

} // namespace

TEST(LibrarySource, FindsALibraryByName) {
    const TempDirectory directory("libsrc_find");
    directory.write("libfoo.so", "abc");

    const auto source = core::directoryLibrarySource({directory.path()});
    const auto bytes = source("libfoo.so");

    ASSERT_TRUE(bytes.ok()) << bytes.error();
    EXPECT_EQ(bytes.value().size(), 3U);
}

TEST(LibrarySource, SearchesTheDirectoriesInOrder) {
    const TempDirectory first("libsrc_first");
    const TempDirectory second("libsrc_second");
    first.write("libfoo.so", "1");
    second.write("libfoo.so", "22");
    second.write("libbar.so", "333");

    const auto source = core::directoryLibrarySource({first.path(), second.path()});

    EXPECT_EQ(source("libfoo.so").value().size(), 1U); // the first directory wins
    EXPECT_EQ(source("libbar.so").value().size(), 3U); // found in the second
}

TEST(LibrarySource, ReportsAMissingLibraryAndWhereItLooked) {
    const TempDirectory directory("libsrc_missing");

    const auto source = core::directoryLibrarySource({directory.path()});
    const auto bytes = source("libnope.so");

    ASSERT_FALSE(bytes.ok());
    EXPECT_NE(bytes.error().find("libnope.so"), std::string::npos);
    EXPECT_NE(bytes.error().find(directory.path().string()), std::string::npos);
}

TEST(LibrarySource, RefusesNamesThatAreNotPlainFileNames) {
    const TempDirectory directory("libsrc_hostile");
    directory.write("secret.txt", "do not read");
    std::filesystem::create_directories(directory.path() / "sub");

    const auto source = core::directoryLibrarySource({directory.path() / "sub"});

    for (const std::string name : {"../secret.txt", "a/b.so", "/etc/passwd", "..", ".", "", "dir\\file.so"}) {
        const auto bytes = source(name);
        EXPECT_FALSE(bytes.ok()) << "accepted: '" << name << "'";
    }
}
