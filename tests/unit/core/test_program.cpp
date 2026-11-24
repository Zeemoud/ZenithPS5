// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/program.hpp"
#include "cpu/guest_call.hpp"
#include "cpu/guest_stack.hpp"
#include "elf_builder.hpp"

#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

using namespace zenith;
using test::ElfBuilder;

namespace {

constexpr GuestAddr importsBase = 0x2000'0000'0000ULL;
constexpr GuestAddr programBase = 0x2000'0100'0000ULL;
constexpr GuestAddr stackBase = 0x2000'0200'0000ULL;
constexpr u64 slotVaddr = 0x10000;
constexpr u64 dynamicVaddr = 0x20000;

void appendU32(std::vector<u8>& code, u32 value) {
    for (int i = 0; i < 4; ++i) {
        code.push_back(static_cast<u8>(value >> (8 * i)));
    }
}

// Code that calls the function whose address is in the slot (a GOT entry) and returns what it returns, adding
// `plus` to it:  sub rsp,8 ; call [rip+slot] ; add eax,plus ; add rsp,8 ; ret
std::vector<u8> callThroughSlot(u32 plus) {
    std::vector<u8> code = {0x48, 0x83, 0xEC, 0x08, 0xFF, 0x15};
    appendU32(code, static_cast<u32>(slotVaddr - 10)); // RIP after the call (offset 10) to the slot
    if (plus != 0) {
        code.insert(code.end(), {0x83, 0xC0, static_cast<u8>(plus)});
    }
    code.insert(code.end(), {0x48, 0x83, 0xC4, 0x08, 0xC3});
    return code;
}

// A shared object with code at 0, a GOT slot, and the symbols and libraries given.
std::vector<u8> sharedObject(std::vector<u8> code, std::vector<test::SymbolSpec> symbols,
                             std::vector<std::string> needed, std::vector<test::RelocationSpec> relocations = {}) {
    return ElfBuilder{}
        .type(loader::elf::typeSharedObject)
        .entry(0)
        .segment({.vaddr = 0, .bytes = std::move(code)})
        .segment({.flags = loader::elf::flagRead | loader::elf::flagWrite,
                  .vaddr = slotVaddr,
                  .bytes = std::vector<u8>(8, 0)})
        .dynamic(dynamicVaddr, std::move(relocations))
        .symbols(std::move(symbols))
        .needed(std::move(needed))
        .build();
}

test::RelocationSpec gotEntry(u32 symbol) {
    return {.offset = slotVaddr, .type = loader::elf::relocGlobDat, .symbol = symbol};
}

// libb.so exports b_func, which returns 41.
std::vector<u8> libB() {
    return sharedObject({0xB8, 0x29, 0x00, 0x00, 0x00, 0xC3}, {{.name = "b_func", .defined = true, .value = 0}}, {});
}

// liba.so exports a_func, which calls b_func (from libb.so) and adds 1.
std::vector<u8> libA() {
    return sharedObject(callThroughSlot(1), {{.name = "a_func", .defined = true, .value = 0}, {.name = "b_func"}},
                        {"libb.so"}, {gotEntry(2)});
}

// A program that calls a_func (from liba.so).
std::vector<u8> mainProgram() {
    return sharedObject(callThroughSlot(0), {{.name = "a_func"}}, {"liba.so"}, {gotEntry(1)});
}

// Serves libraries from memory and counts the requests.
struct Libraries {
    std::map<std::string, std::vector<u8>> files;
    std::vector<std::string> requested = {};

    core::LibrarySource source() {
        return [this](std::string_view name) -> Result<std::vector<u8>> {
            requested.emplace_back(name);
            const auto found = files.find(std::string(name));
            if (found == files.end()) {
                return makeError("no such file");
            }
            return found->second;
        };
    }
};

} // namespace

class ProgramTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }

    Result<core::LoadedProgram> load(const std::vector<u8>& bytes, core::ImportTable& imports,
                                     const core::ProgramOptions& options) {
        const auto elf = loader::ElfFile::parse(bytes);
        EXPECT_TRUE(elf.ok());
        if (!elf.ok()) {
            return makeError("parse failed: {}", elf.error());
        }
        return core::loadProgram(elf.value(), bytes, space, imports, "program", options);
    }

    core::ProgramOptions optionsFor(Libraries& libraries) {
        core::ProgramOptions options;
        options.imageBase = programBase;
        options.libraries = libraries.source();
        return options;
    }

    memory::AddressSpace space;
};

TEST_F(ProgramTest, ModulesCallEachOtherAcrossThreeLevels) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    Libraries libraries{.files = {{"liba.so", libA()}, {"libb.so", libB()}}};

    const auto program = load(mainProgram(), imports.value(), optionsFor(libraries));
    ASSERT_TRUE(program.ok()) << program.error();
    auto stack = cpu::GuestStack::create(space, stackBase, 256 * KiB, "stack");
    ASSERT_TRUE(stack.ok());
    const auto result = cpu::callGuest(program.value().entry(), stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 42U); // main -> a_func (+1) -> b_func (41)
}

TEST_F(ProgramTest, LoadsTheLibrariesInOrderAtDistinctAddresses) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    Libraries libraries{.files = {{"liba.so", libA()}, {"libb.so", libB()}}};

    const auto program = load(mainProgram(), imports.value(), optionsFor(libraries));

    ASSERT_TRUE(program.ok()) << program.error();
    ASSERT_EQ(program.value().modules.size(), 3U);
    EXPECT_EQ(program.value().modules[0].name, "program");
    EXPECT_EQ(program.value().modules[1].name, "liba.so");
    EXPECT_EQ(program.value().modules[2].name, "libb.so");
    EXPECT_EQ(program.value().modules[0].image.base, programBase);
    EXPECT_GT(program.value().modules[1].image.base, programBase);
    EXPECT_GT(program.value().modules[2].image.base, program.value().modules[1].image.base);
    EXPECT_EQ(program.value().modules[0].image.libraries, (std::vector<std::string>{"liba.so"}));
}

TEST_F(ProgramTest, ALibraryNeededBySeveralModulesIsLoadedOnce) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    Libraries libraries{.files = {{"liba.so", libA()}, {"libb.so", libB()}}};
    // The program needs liba.so and libb.so, and liba.so needs libb.so as well.
    const auto bytes = sharedObject(callThroughSlot(0), {{.name = "a_func"}}, {"liba.so", "libb.so"}, {gotEntry(1)});

    const auto program = load(bytes, imports.value(), optionsFor(libraries));

    ASSERT_TRUE(program.ok()) << program.error();
    EXPECT_EQ(program.value().modules.size(), 3U);
    EXPECT_EQ(libraries.requested.size(), 2U); // one request per file
}

TEST_F(ProgramTest, ALibraryTheHostProvidesNeedsNoFile) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    imports.value().addLibrary("libhost.so");
    Libraries libraries;
    const auto bytes = sharedObject(callThroughSlot(0), {{.name = "unused"}}, {"libhost.so"}, {gotEntry(1)});

    const auto program = load(bytes, imports.value(), optionsFor(libraries));

    ASSERT_TRUE(program.ok()) << program.error();
    EXPECT_EQ(program.value().modules.size(), 1U);
    EXPECT_TRUE(libraries.requested.empty());
}

TEST_F(ProgramTest, ALibraryCanUseTheHostLibraries) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    imports.value().addFunction("libhost.so", "host_answer",
                                [](u64, u64, u64, u64, u64, u64) noexcept -> u64 { return 41; });
    // libc.so exports c_func, which calls the host function host_answer and adds 1.
    Libraries libraries{
        .files = {{"libc.so", sharedObject(callThroughSlot(1),
                                           {{.name = "c_func", .defined = true, .value = 0}, {.name = "host_answer"}},
                                           {"libhost.so"}, {gotEntry(2)})}}};
    imports.value().addLibrary("libhost.so");
    const auto bytes = sharedObject(callThroughSlot(0), {{.name = "c_func"}}, {"libc.so"}, {gotEntry(1)});

    const auto program = load(bytes, imports.value(), optionsFor(libraries));
    ASSERT_TRUE(program.ok()) << program.error();
    auto stack = cpu::GuestStack::create(space, stackBase, 256 * KiB, "stack");
    ASSERT_TRUE(stack.ok());
    const auto result = cpu::callGuest(program.value().entry(), stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(ProgramTest, RefusesALibraryNobodyProvides) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    const usize regionsBefore = space.regionCount();
    core::ProgramOptions options;
    options.imageBase = programBase; // no library source at all

    const auto program = load(mainProgram(), imports.value(), options);

    ASSERT_FALSE(program.ok());
    EXPECT_NE(program.error().find("liba.so"), std::string::npos);
    EXPECT_NE(program.error().find("not provided"), std::string::npos);
    EXPECT_EQ(space.regionCount(), regionsBefore);
}

TEST_F(ProgramTest, ReportsAMissingLibraryFileAndRollsBack) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    const usize regionsBefore = space.regionCount();
    Libraries libraries{.files = {{"liba.so", libA()}}}; // libb.so, needed by liba.so, is missing

    const auto program = load(mainProgram(), imports.value(), optionsFor(libraries));

    ASSERT_FALSE(program.ok());
    EXPECT_NE(program.error().find("libb.so"), std::string::npos);
    EXPECT_EQ(space.regionCount(), regionsBefore); // the program and liba.so were unmapped
}

TEST_F(ProgramTest, RefusesALibraryThatIsNotASharedObject) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    const usize regionsBefore = space.regionCount();
    const auto fixedAddress =
        ElfBuilder{}.entry(0x3000'0000'0000ULL).segment({.vaddr = 0x3000'0000'0000ULL, .bytes = {0xC3}}).build();
    Libraries libraries{.files = {{"liba.so", fixedAddress}, {"libb.so", libB()}}};

    const auto program = load(mainProgram(), imports.value(), optionsFor(libraries));

    ASSERT_FALSE(program.ok());
    EXPECT_NE(program.error().find("not a shared object"), std::string::npos);
    EXPECT_EQ(space.regionCount(), regionsBefore);
}

TEST_F(ProgramTest, RefusesALibraryThatIsNotAnElfFile) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    const usize regionsBefore = space.regionCount();
    Libraries libraries{.files = {{"liba.so", std::vector<u8>(64, 0xAB)}}};

    const auto program = load(mainProgram(), imports.value(), optionsFor(libraries));

    ASSERT_FALSE(program.ok());
    EXPECT_NE(program.error().find("liba.so"), std::string::npos);
    EXPECT_EQ(space.regionCount(), regionsBefore);
}

TEST_F(ProgramTest, ASymbolNoModuleExportsBecomesAnUnimplementedStub) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    Libraries libraries{.files = {{"libb.so", libB()}}};
    // Imports a symbol that libb.so does not export.
    const auto bytes = sharedObject(callThroughSlot(0), {{.name = "nobody_has_this"}}, {"libb.so"}, {gotEntry(1)});

    const auto program = load(bytes, imports.value(), optionsFor(libraries));

    ASSERT_TRUE(program.ok()) << program.error();
    ASSERT_EQ(imports.value().unimplementedNames().size(), 1U);
    EXPECT_EQ(imports.value().unimplementedNames()[0], "nobody_has_this");
}

TEST_F(ProgramTest, TheFirstModuleThatExportsASymbolWins) {
    auto imports = core::ImportTable::create(space, importsBase);
    ASSERT_TRUE(imports.ok());
    // Both libraries export "shared_name"; the program needs lib1.so first, so its version is the one used.
    auto exporter = [](u32 value) {
        return sharedObject({0xB8, static_cast<u8>(value), 0x00, 0x00, 0x00, 0xC3},
                            {{.name = "shared_name", .defined = true, .value = 0}}, {});
    };
    Libraries libraries{.files = {{"lib1.so", exporter(11)}, {"lib2.so", exporter(22)}}};
    const auto bytes =
        sharedObject(callThroughSlot(0), {{.name = "shared_name"}}, {"lib1.so", "lib2.so"}, {gotEntry(1)});

    const auto program = load(bytes, imports.value(), optionsFor(libraries));
    ASSERT_TRUE(program.ok()) << program.error();
    auto stack = cpu::GuestStack::create(space, stackBase, 256 * KiB, "stack");
    ASSERT_TRUE(stack.ok());
    const auto result = cpu::callGuest(program.value().entry(), stack.value());

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 11U);
}
