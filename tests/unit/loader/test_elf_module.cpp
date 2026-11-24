// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "elf_builder.hpp"
#include "loader/elf/elf_loader.hpp"

#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

using namespace zenith;
using test::ElfBuilder;
using test::HashStyle;
using test::SymbolSpec;

namespace {

constexpr GuestAddr base = 0x2000'0000'0000ULL;
constexpr u64 slotVaddr = 0x10000;
constexpr u64 dynamicVaddr = 0x20000;

// A shared library: one code page at 0, a data slot, and a dynamic section with `symbols`.
std::vector<u8> library(HashStyle style, std::vector<SymbolSpec> symbols, std::vector<std::string> needed = {},
                        std::vector<test::RelocationSpec> relocations = {}) {
    return ElfBuilder{}
        .type(loader::elf::typeSharedObject)
        .entry(0)
        .segment({.vaddr = 0, .bytes = {0xC3}, .memsize = 0x100})
        .segment({.flags = loader::elf::flagRead | loader::elf::flagWrite,
                  .vaddr = slotVaddr,
                  .bytes = std::vector<u8>(16, 0)})
        .dynamic(dynamicVaddr, std::move(relocations))
        .symbols(std::move(symbols))
        .needed(std::move(needed))
        .hashStyle(style)
        .build();
}

u64 readU64(GuestAddr address) {
    u64 value = 0;
    std::memcpy(&value, memory::AddressSpace::hostPointer(address), sizeof(value));
    return value;
}

} // namespace

class ElfModuleTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    Result<loader::Module> map(const std::vector<u8>& bytes, GuestAddr at = base, std::string_view name = "lib.so") {
        const auto elf = loader::ElfFile::parse(bytes);
        EXPECT_TRUE(elf.ok());
        if (!elf.ok()) {
            return makeError("parse failed: {}", elf.error());
        }
        return loader::mapModule(elf.value(), bytes, space, name, at);
    }

    std::map<std::string, GuestAddr> exportsOf(const loader::Module& module) {
        const auto exports = loader::exportedSymbols(module);
        EXPECT_TRUE(exports.ok());
        std::map<std::string, GuestAddr> result;
        if (exports.ok()) {
            for (const auto& symbol : exports.value()) {
                result[symbol.name] = symbol.address;
            }
        }
        return result;
    }

    void checkExports(HashStyle style) {
        const auto module = map(library(style, {{.name = "add", .defined = true, .value = 0x10},
                                                {.name = "mul", .defined = true, .value = 0x20},
                                                {.name = "imported"}}));
        ASSERT_TRUE(module.ok()) << module.error();

        const auto exports = exportsOf(module.value());

        ASSERT_EQ(exports.size(), 2U);
        EXPECT_EQ(exports.at("add"), base + 0x10);
        EXPECT_EQ(exports.at("mul"), base + 0x20);
    }

    memory::AddressSpace space;
};

TEST_F(ElfModuleTest, ExportsTheDefinedSymbolsWithAGnuHashTable) {
    checkExports(HashStyle::Gnu);
}

TEST_F(ElfModuleTest, ExportsTheDefinedSymbolsWithAClassicHashTable) {
    checkExports(HashStyle::Sysv);
}

TEST_F(ElfModuleTest, DoesNotExportImportsLocalsOrHiddenSymbols) {
    const auto module =
        map(library(HashStyle::Gnu, {{.name = "imported"},
                                     {.name = "local_one", .defined = true, .value = 0x10, .local = true},
                                     {.name = "hidden_one", .defined = true, .value = 0x20, .hidden = true},
                                     {.name = "visible", .defined = true, .value = 0x30}}));
    ASSERT_TRUE(module.ok());

    const auto exports = exportsOf(module.value());

    ASSERT_EQ(exports.size(), 1U);
    EXPECT_TRUE(exports.count("visible") == 1);
}

TEST_F(ElfModuleTest, ExportsWeakSymbols) {
    const auto module = map(library(HashStyle::Gnu, {{.name = "maybe", .defined = true, .value = 0x10, .weak = true}}));
    ASSERT_TRUE(module.ok());

    EXPECT_EQ(exportsOf(module.value()).count("maybe"), 1U);
}

TEST_F(ElfModuleTest, AModuleWithoutSymbolsExportsNothing) {
    for (const HashStyle style : {HashStyle::Gnu, HashStyle::Sysv}) {
        memory::AddressSpace local;
        const auto bytes = library(style, {}, {"libx.so"});
        const auto elf = loader::ElfFile::parse(bytes);
        ASSERT_TRUE(elf.ok());
        auto module = loader::mapModule(elf.value(), bytes, local, "lib.so", base);
        ASSERT_TRUE(module.ok());

        const auto exports = loader::exportedSymbols(module.value());

        ASSERT_TRUE(exports.ok()) << exports.error();
        EXPECT_TRUE(exports.value().empty());
    }
}

TEST_F(ElfModuleTest, WithoutAHashTableTheSymbolCountIsUnknown) {
    const auto module = map(library(HashStyle::None, {{.name = "add", .defined = true, .value = 0x10}}));
    ASSERT_TRUE(module.ok());

    const auto exports = loader::exportedSymbols(module.value());

    ASSERT_FALSE(exports.ok());
    EXPECT_NE(exports.error().find("how many symbols"), std::string::npos);
}

TEST_F(ElfModuleTest, ListsTheLibrariesItNeeds) {
    const auto module = map(library(HashStyle::Gnu, {}, {"liba.so", "libb.so"}));
    ASSERT_TRUE(module.ok());

    const auto needed = loader::neededLibraries(module.value());

    ASSERT_TRUE(needed.ok()) << needed.error();
    EXPECT_EQ(needed.value(), (std::vector<std::string>{"liba.so", "libb.so"}));
}

TEST_F(ElfModuleTest, MappingLeavesTheModuleWritableUntilItIsProtected) {
    auto module = map(library(HashStyle::Gnu, {}));
    ASSERT_TRUE(module.ok());
    EXPECT_EQ(space.find(base)->protection, platform::readWrite); // the code page is still writable

    ASSERT_TRUE(loader::protectModule(module.value(), space).ok());

    EXPECT_EQ(space.find(base)->protection, platform::readExecute); // the code segment is read + execute
}

TEST_F(ElfModuleTest, UnmappingGivesEverythingBack) {
    auto module = map(library(HashStyle::Gnu, {}));
    ASSERT_TRUE(module.ok());
    ASSERT_GT(space.regionCount(), 0U);

    loader::unmapModule(module.value(), space);

    EXPECT_EQ(space.regionCount(), 0U);
}

TEST_F(ElfModuleTest, AModuleCanImportFromAnother) {
    // Library A exports "answer"; library B imports it. Both are mapped first, then B is relocated against A.
    auto a = map(library(HashStyle::Gnu, {{.name = "answer", .defined = true, .value = 0x40}}), base, "a.so");
    auto b = map(library(HashStyle::Gnu, {{.name = "answer"}}, {"a.so"},
                         {{.offset = slotVaddr, .type = loader::elf::relocGlobDat, .symbol = 1}}),
                 base + 0x100000, "b.so");
    ASSERT_TRUE(a.ok()) << a.error();
    ASSERT_TRUE(b.ok()) << b.error();

    const auto scope = exportsOf(a.value());
    const auto relocated = loader::relocateModule(b.value(), [&scope](std::string_view name) -> Result<GuestAddr> {
        const auto found = scope.find(std::string(name));
        if (found == scope.end()) {
            return makeError("not found");
        }
        return found->second;
    });

    ASSERT_TRUE(relocated.ok()) << relocated.error();
    EXPECT_EQ(readU64(base + 0x100000 + slotVaddr), base + 0x40);
    ASSERT_EQ(b.value().image.imports.size(), 1U);
    EXPECT_EQ(b.value().image.imports[0].name, "answer");
}

TEST_F(ElfModuleTest, MappingTwoModulesAtTheSameBaseFailsCleanly) {
    auto first = map(library(HashStyle::Gnu, {}), base, "first.so");
    ASSERT_TRUE(first.ok());
    const usize regionsBefore = space.regionCount();

    const auto second = map(library(HashStyle::Gnu, {}), base, "second.so");

    ASSERT_FALSE(second.ok());
    EXPECT_NE(second.error().find("cannot map"), std::string::npos);
    EXPECT_EQ(space.regionCount(), regionsBefore); // nothing of the second one stays mapped
}

TEST_F(ElfModuleTest, MappingDoesNotRequireAMeaningfulEntryPoint) {
    // Linkers leave the entry point of a shared library at 0, which is not in an executable segment.
    const auto bytes = ElfBuilder{}
                           .type(loader::elf::typeSharedObject)
                           .entry(0x10)
                           .segment({.flags = loader::elf::flagRead, .vaddr = 0, .bytes = {1, 2, 3, 4}})
                           .segment({.vaddr = 0x10000, .bytes = {0xC3}})
                           .build();

    const auto module = map(bytes);

    ASSERT_TRUE(module.ok()) << module.error();
    const auto entry = loader::checkEntryPoint(module.value());
    ASSERT_FALSE(entry.ok());
    EXPECT_NE(entry.error().find("entry point"), std::string::npos);
}

TEST_F(ElfModuleTest, TheEntryPointCheckAcceptsAnExecutableSegment) {
    const auto bytes = ElfBuilder{}
                           .type(loader::elf::typeSharedObject)
                           .entry(0x10000)
                           .segment({.flags = loader::elf::flagRead, .vaddr = 0, .bytes = {1, 2, 3, 4}})
                           .segment({.vaddr = 0x10000, .bytes = {0xC3}})
                           .build();

    const auto module = map(bytes);

    ASSERT_TRUE(module.ok()) << module.error();
    EXPECT_TRUE(loader::checkEntryPoint(module.value()).ok());
}
