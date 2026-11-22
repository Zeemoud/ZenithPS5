// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/run_elf.hpp"
#include "elf_builder.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace zenith;
using test::ElfBuilder;

namespace {

constexpr GuestAddr codeBase = 0x2000'0000'0000ULL;

// mov eax, 42 ; ret
const std::vector<u8> returns42 = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};

std::vector<u8> program(const std::vector<u8>& code) {
    return ElfBuilder{}.entry(codeBase).segment({.vaddr = codeBase, .bytes = code}).build();
}

} // namespace

class RunElfTest : public ::testing::Test {
protected:
    void SetUp() override { log::setLevel(log::Level::Off); }
    void TearDown() override { log::setLevel(log::Level::Info); }
};

TEST_F(RunElfTest, RunsAProgramAndReturnsItsResult) {
    const auto result = core::runElf(program(returns42), "test.elf");

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(RunElfTest, ReleasesEverythingSoItCanRunAgain) {
    const auto bytes = program(returns42);

    for (int i = 0; i < 3; ++i) {
        const auto result = core::runElf(bytes, "test.elf");
        ASSERT_TRUE(result.ok()) << result.error();
        ASSERT_EQ(result.value(), 42U);
    }
}

TEST_F(RunElfTest, ReportsAnInvalidFileWithItsName) {
    const std::vector<u8> garbage(100, 0xAB);

    const auto result = core::runElf(garbage, "garbage.bin");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("garbage.bin"), std::string::npos);
}

TEST_F(RunElfTest, ReportsALoadFailure) {
    // The entry point is outside the only segment.
    const auto bytes = ElfBuilder{}.entry(codeBase + 0x100000).segment({.vaddr = codeBase, .bytes = returns42}).build();

    const auto result = core::runElf(bytes, "bad_entry.elf");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("entry point"), std::string::npos);
}

TEST_F(RunElfTest, HonorsTheStackSizeOption) {
    core::RunOptions options;
    options.stackSize = 0; // too small to hold even the guard area

    const auto result = core::runElf(program(returns42), "test.elf", options);

    EXPECT_FALSE(result.ok());
}

namespace {

// A position-independent program: loads a pointer from a relocated slot and returns it.
//   mov rax, [rip + disp32] ; ret        with the slot at link-time address 0x10000
std::vector<u8> relocatedPointerProgram() {
    constexpr u64 slot = 0x10000;
    constexpr u32 displacement = static_cast<u32>(slot - 7); // RIP after the 7-byte mov is at 7
    std::vector<u8> code = {0x48, 0x8B, 0x05};
    for (int i = 0; i < 4; ++i) {
        code.push_back(static_cast<u8>(displacement >> (8 * i)));
    }
    code.push_back(0xC3);

    return ElfBuilder{}
        .type(loader::elf::typeSharedObject)
        .entry(0)
        .segment({.vaddr = 0, .bytes = code})
        .segment(
            {.flags = loader::elf::flagRead | loader::elf::flagWrite, .vaddr = slot, .bytes = std::vector<u8>(8, 0)})
        .dynamic(0x20000, {{.offset = slot, .addend = 0x1234}})
        .build();
}

} // namespace

TEST_F(RunElfTest, RunsAPositionIndependentProgramAtTheDefaultBase) {
    const auto result = core::runElf(relocatedPointerProgram(), "pie.elf");

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), core::RunOptions{}.imageBase + 0x1234);
}

TEST_F(RunElfTest, RunsAPositionIndependentProgramAtTheRequestedBase) {
    core::RunOptions options;
    options.imageBase = 0x2000'0000'0000ULL;

    const auto result = core::runElf(relocatedPointerProgram(), "pie.elf", options);

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), options.imageBase + 0x1234);
}

namespace {

constexpr u64 gotSlot = 0x10000;
constexpr u64 messageVaddr = 0x14000;
const std::string importedMessage = "Hello import\n";

void appendU32(std::vector<u8>& code, u32 value) {
    for (int i = 0; i < 4; ++i) {
        code.push_back(static_cast<u8>(value >> (8 * i)));
    }
}

// A position-independent program that imports one function through a GOT slot and calls it.
//   sub rsp, 8 ; <set the arguments> ; call [rip + slot] ; add rsp, 8 ; ret
std::vector<u8> importingProgram(std::vector<u8> setup, usize callOffset, const std::string& symbol,
                                 const std::string& library) {
    std::vector<u8> code = {0x48, 0x83, 0xEC, 0x08};
    code.insert(code.end(), setup.begin(), setup.end());
    code.insert(code.end(), {0xFF, 0x15}); // call [rip + disp32]
    appendU32(code, static_cast<u32>(gotSlot - (callOffset + 6)));
    code.insert(code.end(), {0x48, 0x83, 0xC4, 0x08, 0xC3});

    return ElfBuilder{}
        .type(loader::elf::typeSharedObject)
        .entry(0)
        .segment({.vaddr = 0, .bytes = code})
        .segment(
            {.flags = loader::elf::flagRead | loader::elf::flagWrite, .vaddr = gotSlot, .bytes = std::vector<u8>(8, 0)})
        .segment({.flags = loader::elf::flagRead,
                  .vaddr = messageVaddr,
                  .bytes = std::vector<u8>(importedMessage.begin(), importedMessage.end())})
        .dynamic(0x20000, {{.offset = gotSlot, .type = loader::elf::relocGlobDat, .symbol = 1}})
        .symbols({{.name = symbol}})
        .needed({library})
        .build();
}

// mov edi, <value>
std::vector<u8> setFirstArgument(u32 value) {
    std::vector<u8> code = {0xBF};
    appendU32(code, value);
    return code;
}

} // namespace

TEST_F(RunElfTest, ABoundImportCallsTheHostFunction) {
    // exit(5) through the import "zenith_exit": the program ends with 5.
    const auto bytes = importingProgram(setFirstArgument(5), 9, "zenith_exit", "libzenithtest.so");

    const auto result = core::runElf(bytes, "import.elf");

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 5U);
}

TEST_F(RunElfTest, ABoundImportCanWriteOutput) {
    // write(1, message, length) through the import "zenith_write".
    //   mov edi, 1 ; lea rsi, [rip + message] ; mov edx, length
    std::vector<u8> setup = setFirstArgument(1);
    setup.insert(setup.end(), {0x48, 0x8D, 0x35}); // lea rsi, [rip + disp32]; RIP after it is at offset 16
    appendU32(setup, static_cast<u32>(messageVaddr - 16));
    setup.push_back(0xBA); // mov edx, imm32
    appendU32(setup, static_cast<u32>(importedMessage.size()));
    const auto bytes = importingProgram(setup, 21, "zenith_write", "libzenithtest.so");

    std::vector<std::string> output;
    core::RunOptions options;
    options.output = [&output](int fd, std::string_view text) {
        output.push_back(std::to_string(fd) + ":" + std::string(text));
    };
    const auto result = core::runElf(bytes, "import.elf", options);

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), importedMessage.size());
    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0], "1:" + importedMessage);
}

TEST_F(RunElfTest, CallingAnUnimplementedImportStopsTheProgramAndNamesIt) {
    const auto bytes = importingProgram(setFirstArgument(5), 9, "sceKernelNotYetThere", "libzenithtest.so");

    const auto result = core::runElf(bytes, "import.elf");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("unimplemented function 'sceKernelNotYetThere'"), std::string::npos);
    EXPECT_NE(result.error().find("import.elf"), std::string::npos);
}

TEST_F(RunElfTest, AnUnimplementedImportThatIsNeverCalledIsHarmless) {
    // The program binds an import it never calls: it loads and runs normally.
    const auto bytes = ElfBuilder{}
                           .type(loader::elf::typeSharedObject)
                           .entry(0)
                           .segment({.vaddr = 0, .bytes = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}}) // mov eax, 42 ; ret
                           .segment({.flags = loader::elf::flagRead | loader::elf::flagWrite,
                                     .vaddr = gotSlot,
                                     .bytes = std::vector<u8>(8, 0)})
                           .dynamic(0x20000, {{.offset = gotSlot, .type = loader::elf::relocGlobDat, .symbol = 1}})
                           .symbols({{.name = "neverCalled"}})
                           .needed({"libzenithtest.so"})
                           .build();

    const auto result = core::runElf(bytes, "import.elf");

    ASSERT_TRUE(result.ok()) << result.error();
    EXPECT_EQ(result.value(), 42U);
}

TEST_F(RunElfTest, RefusesAProgramThatNeedsAnUnknownLibrary) {
    const auto bytes = importingProgram(setFirstArgument(5), 9, "zenith_exit", "libmissing.so");

    const auto result = core::runElf(bytes, "import.elf");

    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().find("libmissing.so"), std::string::npos);
}
