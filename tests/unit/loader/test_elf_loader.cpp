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

    Result<loader::LoadedImage> load(const std::vector<u8>& bytes, const loader::LoadOptions& options = {}) {
        const auto elf = loader::ElfFile::parse(bytes);
        EXPECT_TRUE(elf.ok());
        if (!elf.ok()) {
            return makeError("parse failed: {}", elf.error());
        }
        return loader::loadElf(elf.value(), bytes, space, "test.elf", options);
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

TEST_F(ElfLoaderTest, AcceptsASegmentThatDoesNotStartOnAPageBoundary) {
    // Like the last segment a linker produces (it starts at 0xfef0 in real files).
    const GuestAddr start = base + 0x3EF0;
    const auto bytes = ElfBuilder{}.entry(start).segment(codeSegment(start)).build();

    const auto image = load(bytes);

    ASSERT_TRUE(image.ok()) << image.error();
    ASSERT_EQ(image.value().segments.size(), 1U);
    EXPECT_EQ(image.value().segments[0].start, base); // the page around it is mapped
    EXPECT_EQ(image.value().segments[0].size, guestPageSize);
    EXPECT_EQ(std::memcmp(memory::AddressSpace::hostPointer(start), codeBytes.data(), codeBytes.size()), 0);
}

TEST_F(ElfLoaderTest, MapsEveryPageOfASegmentThatCrossesAPageBoundary) {
    const GuestAddr start = base + 0x3EF0;
    const auto bytes =
        ElfBuilder{}.entry(start).segment({.vaddr = start, .bytes = codeBytes, .memsize = 0x200}).build();

    const auto image = load(bytes);

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().segments[0].size, 2 * guestPageSize);
}

TEST_F(ElfLoaderTest, RejectsSegmentsThatShareAPage) {
    const auto bytes = ElfBuilder{}
                           .entry(base)
                           .segment(codeSegment(base))
                           .segment({.flags = loader::elf::flagRead, .vaddr = base + 0x100, .bytes = dataBytes})
                           .build();

    const auto image = load(bytes);

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("cannot map"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

namespace {

constexpr u64 slotVaddr = 0x10000;
constexpr u64 dynamicVaddr = 0x20000;

// A position-independent image: code at 0, an 8-byte slot at slotVaddr, the dynamic section at dynamicVaddr.
std::vector<u8> positionIndependent(std::vector<test::RelocationSpec> relocations,
                                    std::vector<std::pair<i64, u64>> tags = {},
                                    u32 slotFlags = loader::elf::flagRead | loader::elf::flagWrite) {
    return ElfBuilder{}
        .type(loader::elf::typeSharedObject)
        .entry(1)
        .segment(codeSegment(0))
        .segment({.flags = slotFlags, .vaddr = slotVaddr, .bytes = std::vector<u8>(8, 0x11)})
        .dynamic(dynamicVaddr, std::move(relocations), std::move(tags))
        .build();
}

u64 readU64(GuestAddr address) {
    u64 value = 0;
    std::memcpy(&value, memory::AddressSpace::hostPointer(address), sizeof(value));
    return value;
}

} // namespace

TEST_F(ElfLoaderTest, MapsAPositionIndependentImageAtTheRequestedBase) {
    const auto image = load(positionIndependent({}), {.baseAddress = base});

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().base, base);
    EXPECT_EQ(image.value().entry, base + 1);
    ASSERT_TRUE(space.find(base).has_value());
    EXPECT_EQ(space.find(base)->protection, platform::readExecute);
    ASSERT_TRUE(space.find(base + slotVaddr).has_value());
    EXPECT_EQ(space.find(base + slotVaddr)->protection, platform::readWrite);
    ASSERT_TRUE(space.find(base + dynamicVaddr).has_value());
    EXPECT_EQ(space.find(base + dynamicVaddr)->protection, platform::Protection::Read);
}

TEST_F(ElfLoaderTest, UsesTheDefaultBaseWhenNoneIsGiven) {
    const auto image = load(positionIndependent({}));

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().base, loader::LoadOptions{}.baseAddress);
}

TEST_F(ElfLoaderTest, AppliesRelativeRelocations) {
    const auto image = load(positionIndependent({{.offset = slotVaddr, .addend = 0x1234}}), {.baseAddress = base});

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().relocationCount, 1U);
    EXPECT_EQ(readU64(base + slotVaddr), base + 0x1234);
}

TEST_F(ElfLoaderTest, RelocatesTargetsInReadOnlySegments) {
    const auto image = load(positionIndependent({{.offset = slotVaddr, .addend = 0x40}}, {}, loader::elf::flagRead),
                            {.baseAddress = base});

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(readU64(base + slotVaddr), base + 0x40);
    EXPECT_EQ(space.find(base + slotVaddr)->protection, platform::Protection::Read); // protected afterwards
}

TEST_F(ElfLoaderTest, IgnoresRelocationsOfTypeNone) {
    const auto image =
        load(positionIndependent({{.offset = slotVaddr, .type = loader::elf::relocNone, .addend = 0x1234}}),
             {.baseAddress = base});

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().relocationCount, 0U);
    EXPECT_EQ(readU64(base + slotVaddr), 0x1111111111111111ULL);
}

TEST_F(ElfLoaderTest, RejectsUnsupportedRelocationTypes) {
    const auto image = load(positionIndependent({{.offset = slotVaddr, .type = 7, .addend = 0}}), // JUMP_SLOT
                            {.baseAddress = base});

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("not supported"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsRelocationTargetsOutsideTheImage) {
    const auto image = load(positionIndependent({{.offset = 0x900000, .addend = 0}}), {.baseAddress = base});

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("outside the image"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsSharedLibraryDependencies) {
    const auto image = load(positionIndependent({}, {{loader::elf::dynNeeded, 1}}), {.baseAddress = base});

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("shared libraries"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsPackedRelocations) {
    const auto image = load(positionIndependent({}, {{loader::elf::dynRelr, dynamicVaddr}}), {.baseAddress = base});

    ASSERT_FALSE(image.ok());
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, RejectsAnUnalignedBaseAddress) {
    const auto image = load(positionIndependent({}), {.baseAddress = base + 0x1000});

    ASSERT_FALSE(image.ok());
    EXPECT_NE(image.error().find("aligned"), std::string::npos);
    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfLoaderTest, FixedAddressExecutablesIgnoreTheBaseAddress) {
    const auto bytes = ElfBuilder{}.entry(base).segment(codeSegment(base)).build();

    const auto image = load(bytes, {.baseAddress = base + 0x100000});

    ASSERT_TRUE(image.ok()) << image.error();
    EXPECT_EQ(image.value().base, 0U);
    EXPECT_EQ(image.value().entry, base);
    EXPECT_TRUE(space.find(base).has_value());
}
