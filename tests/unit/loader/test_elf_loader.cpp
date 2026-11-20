// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "elf_builder.hpp"
#include "loader/elf/elf_loader.hpp"

#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace zenith;
using memory::guestPageSize;
using test::ElfBuilder;
using test::SegmentSpec;

namespace {

constexpr GuestAddr base = 0x2000'0000'0000ULL;
constexpr u64 dataOffsetInSpace = 4 * guestPageSize;

int hostVariable = 0; // lives in the host's own data segment: certainly mapped

const std::vector<u8> codeBytes = {0x90, 0x90, 0x90, 0xC3};
const std::vector<u8> dataBytes = {1, 2, 3, 4, 5, 6, 7, 8};

SegmentSpec codeSegment(GuestAddr vaddr) {
    return {
        .flags = loader::elf::flagRead | loader::elf::flagExecute, .vaddr = vaddr, .bytes = codeBytes, .memsize = 100};
}

SegmentSpec dataSegment(GuestAddr vaddr) {
    return {.flags = loader::elf::flagRead | loader::elf::flagWrite,
            .vaddr = vaddr,
            .bytes = dataBytes,
            .memsize = 2 * guestPageSize};
}

} // namespace

class ElfLoaderTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    Result<loader::LoadedImage> load(const std::vector<u8>& bytes) {
        const auto elf = loader::ElfFile::parse(bytes);
        EXPECT_TRUE(elf.ok());
        if (!elf.ok()) {
            return makeError("parse failed: {}", elf.error());
        }
        return loader::loadElf(elf.value(), bytes, space, "test.elf");
    }

    memory::AddressSpace space;
};

TEST_F(ElfLoaderTest, MapsSegmentsWithTheirContentAndProtection) {
    const auto bytes =
        ElfBuilder{}.entry(base + 1).segment(codeSegment(base)).segment(dataSegment(base + dataOffsetInSpace)).build();

    const auto image = load(bytes);

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().entry, base + 1);
    ASSERT_EQ(image.value().segments.size(), 2U);

    const auto code = space.find(base);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(code->protection, platform::readExecute);
    EXPECT_EQ(code->size, guestPageSize); // 100 bytes rounded up to a guest page
    EXPECT_EQ(std::memcmp(memory::AddressSpace::hostPointer(base), codeBytes.data(), codeBytes.size()), 0);

    const auto data = space.find(base + dataOffsetInSpace);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->protection, platform::readWrite);
    EXPECT_EQ(data->size, 2 * guestPageSize);
    EXPECT_EQ(
        std::memcmp(memory::AddressSpace::hostPointer(base + dataOffsetInSpace), dataBytes.data(), dataBytes.size()),
        0);
}

TEST_F(ElfLoaderTest, ZeroFillsTheMemoryBeyondTheFileData) {
    const auto bytes =
        ElfBuilder{}.entry(base).segment(codeSegment(base)).segment(dataSegment(base + dataOffsetInSpace)).build();
    ASSERT_TRUE(load(bytes).ok());

    const u8* bss = memory::AddressSpace::hostPointer(base + dataOffsetInSpace);
    EXPECT_TRUE(std::all_of(bss + dataBytes.size(), bss + 2 * guestPageSize, [](u8 byte) { return byte == 0; }));
}

TEST_F(ElfLoaderTest, WritableSegmentsAreWritable) {
    const auto bytes =
        ElfBuilder{}.entry(base).segment(codeSegment(base)).segment(dataSegment(base + dataOffsetInSpace)).build();
    ASSERT_TRUE(load(bytes).ok());

    u8* data = memory::AddressSpace::hostPointer(base + dataOffsetInSpace);
    data[0] = 0xEE;

    EXPECT_EQ(data[0], 0xEE);
}

TEST_F(ElfLoaderTest, IgnoresNonLoadSegments) {
    const auto bytes = ElfBuilder{}
                           .entry(base)
                           .segment(codeSegment(base))
                           .segment({.type = 4, .flags = 4, .vaddr = 0, .bytes = {}, .memsize = 0})
                           .build();

    const auto image = load(bytes);

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().segments.size(), 1U);
    EXPECT_EQ(space.regionCount(), 1U);
}

TEST_F(ElfLoaderTest, RejectsAnUnalignedSegment) {
    const auto bytes = ElfBuilder{}.entry(base + 0x1000).segment(codeSegment(base + 0x1000)).build();

    const auto image = load(bytes);

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("aligned"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsAnEntryPointOutsideExecutableSegments) {
    const auto bytes = ElfBuilder{}
                           .entry(base + dataOffsetInSpace)
                           .segment(codeSegment(base))
                           .segment(dataSegment(base + dataOffsetInSpace))
                           .build();

    const auto image = load(bytes);

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("entry point"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsAFileWithoutLoadableSegment) {
    const auto bytes =
        ElfBuilder{}.entry(base).segment({.type = 4, .flags = 4, .vaddr = 0, .bytes = {}, .memsize = 0}).build();

    EXPECT_FALSE(load(bytes).ok());
}

TEST_F(ElfLoaderTest, RollsBackWhenASegmentOverlapsAnother) {
    const auto bytes = ElfBuilder{}
                           .entry(base)
                           .segment(codeSegment(base))
                           .segment(dataSegment(base)) // same address as the first one
                           .build();

    EXPECT_FALSE(load(bytes).ok());

    EXPECT_EQ(space.regionCount(), 0U);
    EXPECT_TRUE(space.map(base, guestPageSize, platform::readWrite, "free again"));
}

TEST_F(ElfLoaderTest, RollsBackWhenASegmentHitsAHostMapping) {
    const GuestAddr hostPage = memory::AddressSpace::guestAddress(&hostVariable) & ~(guestPageSize - 1);
    const auto bytes = ElfBuilder{}.entry(base).segment(codeSegment(base)).segment(dataSegment(hostPage)).build();

    EXPECT_FALSE(load(bytes).ok());

    hostVariable = 7; // would crash if the host mapping had been replaced
    EXPECT_EQ(hostVariable, 7);
    EXPECT_EQ(space.regionCount(), 0U);
}
