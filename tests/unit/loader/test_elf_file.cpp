// SPDX-License-Identifier: GPL-2.0-only
#include "elf_builder.hpp"
#include "loader/elf/elf_file.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace zenith;
using test::ElfBuilder;
using test::SegmentSpec;

namespace {

// Offsets inside the ELF64 header, used to corrupt a valid file.
constexpr usize offsetIdentClass = 4;
constexpr usize offsetIdentData = 5;
constexpr usize offsetType = 16;
constexpr usize offsetMachine = 18;
constexpr usize offsetPhoff = 32;
constexpr usize offsetPhentsize = 54;
constexpr usize offsetPhnum = 56;

template <class T> void poke(std::vector<u8>& bytes, usize offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<u8> validFile() {
    return ElfBuilder{}
        .entry(0x10000)
        .segment({.vaddr = 0x10000, .bytes = {0x90, 0x90, 0xC3}})
        .segment({.flags = loader::elf::flagRead | loader::elf::flagWrite, .vaddr = 0x20000, .bytes = {1, 2, 3, 4}})
        .build();
}

} // namespace

TEST(ElfFile, ParsesAValidFile) {
    const auto bytes = validFile();

    const auto elf = loader::ElfFile::parse(bytes);

    ASSERT_TRUE(elf.ok()) << elf.error();
    EXPECT_EQ(elf.value().entry(), 0x10000U);
    ASSERT_EQ(elf.value().programHeaders().size(), 2U);
    EXPECT_EQ(elf.value().programHeaders()[1].vaddr, 0x20000U);
    EXPECT_EQ(elf.value().programHeaders()[1].filesz, 4U);
}

TEST(ElfFile, RejectsAFileThatIsTooSmall) {
    const std::vector<u8> bytes(10, 0);

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsABadMagic) {
    auto bytes = validFile();
    bytes[1] = 'X';

    const auto elf = loader::ElfFile::parse(bytes);

    ASSERT_FALSE(elf.ok());
    EXPECT_NE(elf.error().find("magic"), std::string::npos);
}

TEST(ElfFile, RejectsA32BitFile) {
    auto bytes = validFile();
    bytes[offsetIdentClass] = 1;

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsABigEndianFile) {
    auto bytes = validFile();
    bytes[offsetIdentData] = 2;

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsAnotherMachine) {
    auto bytes = validFile();
    poke<u16>(bytes, offsetMachine, 183); // AArch64

    const auto elf = loader::ElfFile::parse(bytes);

    ASSERT_FALSE(elf.ok());
    EXPECT_NE(elf.error().find("machine"), std::string::npos);
}

TEST(ElfFile, RejectsNonExecutableTypes) {
    auto bytes = validFile();
    poke<u16>(bytes, offsetType, 3); // ET_DYN

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsAWrongProgramHeaderSize) {
    auto bytes = validFile();
    poke<u16>(bytes, offsetPhentsize, 32);

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsAnEmptyProgramHeaderTable) {
    auto bytes = validFile();
    poke<u16>(bytes, offsetPhnum, 0);

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsAProgramHeaderTableOutsideTheFile) {
    auto bytes = validFile();
    poke<u64>(bytes, offsetPhoff, ~0ULL - 8);

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, RejectsSegmentDataOutsideTheFile) {
    auto bytes = validFile();
    bytes.resize(bytes.size() - 16);

    const auto elf = loader::ElfFile::parse(bytes);

    ASSERT_FALSE(elf.ok());
    EXPECT_NE(elf.error().find("outside the file"), std::string::npos);
}

TEST(ElfFile, RejectsAFileSizeLargerThanTheMemorySize) {
    const auto bytes =
        ElfBuilder{}.entry(0x10000).segment({.vaddr = 0x10000, .bytes = std::vector<u8>(16, 0), .memsize = 4}).build();

    EXPECT_FALSE(loader::ElfFile::parse(bytes).ok());
}

TEST(ElfFile, IgnoresNonLoadSegmentsWhenValidating) {
    const auto bytes = ElfBuilder{}
                           .entry(0x10000)
                           .segment({.vaddr = 0x10000, .bytes = {0xC3}})
                           .segment({.type = 4, .flags = 4, .vaddr = 0, .bytes = {}, .memsize = 0})
                           .build();

    const auto elf = loader::ElfFile::parse(bytes);

    ASSERT_TRUE(elf.ok()) << elf.error();
    EXPECT_EQ(elf.value().programHeaders().size(), 2U);
}
