// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "loader/elf/elf_file.hpp"
#include "memory/address_space.hpp"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zenith::loader {

// A mapped region of the image: whole guest pages, starting at or before the first byte of a segment.
struct LoadedSegment {
    GuestAddr start = 0;
    u64 size = 0;
};

// A symbol the program imports, and the guest address it was bound to.
struct ResolvedImport {
    std::string name;
    GuestAddr address = 0;
};

struct LoadedImage {
    GuestAddr base = 0; // load bias: 0 for ET_EXEC, the chosen base address for ET_DYN
    GuestAddr entry = 0;
    usize relocationCount = 0;
    std::vector<LoadedSegment> segments;
    std::vector<std::string> libraries;  // DT_NEEDED names, all provided by the host
    std::vector<ResolvedImport> imports; // imported symbols, in the order they were first needed
};

// Returns the guest address an imported symbol is bound to, or an error.
using SymbolResolver = std::function<Result<GuestAddr>(std::string_view)>;

struct LoadOptions {
    // Where a position-independent image (ET_DYN) is loaded. Must be a multiple of the guest page size.
    // Ignored for fixed-address executables (ET_EXEC), which go where they were linked.
    GuestAddr baseAddress = 0x400000;

    // The host provides the libraries a program is linked against (DT_NEEDED), as HLE libraries.
    // Both callbacks are optional; without them, a program that needs a library or an import is refused.
    //
    // hasLibrary: true if the host provides the library with this name (for example "libkernel.so").
    // resolveSymbol: returns the guest address to bind an imported symbol to, or an error. It is called at
    //   most once per symbol. It may map memory (for example stubs) but must not touch the image.
    std::function<bool(std::string_view)> hasLibrary;
    SymbolResolver resolveSymbol;
};

// ---------------------------------------------------------------------------------------------------------
// Loading one image in steps. This is what loadElf() does, and what a program with several modules needs:
// all the modules must be mapped before any of them is relocated, because they import from each other.
//
//   mapModule        map the segments (writable), copy their contents
//   neededLibraries  names of the libraries it asks for (DT_NEEDED)
//   exportedSymbols  the symbols it offers to the others
//   relocateModule   apply the relocations, binding imports through a resolver
//   protectModule    set the final protections: the module is ready to run
//   unmapModule      take it out again (to roll back)
// ---------------------------------------------------------------------------------------------------------

struct SegmentPlan {
    GuestAddr regionStart = 0;
    u64 regionSize = 0;
    platform::Protection protection = platform::Protection::None;
    GuestAddr start = 0; // exact address of the first byte of the segment
    u64 memorySize = 0;  // size of the segment in memory, without the rounding to pages
};

// One ELF image in the address space. Plain data: the address space owns the memory, and unmapModule() gives
// it back.
struct Module {
    Module(std::string moduleName, ElfFile file) : name(std::move(moduleName)), elf(std::move(file)) {}

    std::string name;
    ElfFile elf;
    LoadedImage image;
    std::vector<SegmentPlan> plans;
};

struct ExportedSymbol {
    std::string name;
    GuestAddr address = 0;
};

// Maps the PT_LOAD segments of `elf` into `space` (writable for now) and copies their contents (the part beyond
// the file data is zero-filled). `data` must be the bytes `elf` was parsed from. `baseAddress` is where an ET_DYN
// image goes (a multiple of the guest page size); ET_EXEC images go where they were linked.
//
// A segment does not need to start on a page boundary: the pages around it are mapped, and its bytes are put
// at their exact address. Two segments must not share a page (linkers keep them apart with
// -z max-page-size=0x4000), because each page has a single protection.
//
// On failure nothing stays mapped.
[[nodiscard]] Result<Module> mapModule(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                       std::string_view name, GuestAddr baseAddress);

// Checks that the entry point is inside an executable segment. This is required of the program that is run,
// not of its libraries (the linker leaves their entry point meaningless).
[[nodiscard]] Status checkEntryPoint(const Module& module);

// The DT_NEEDED names of a mapped module.
[[nodiscard]] Result<std::vector<std::string>> neededLibraries(const Module& module);

// The symbols a mapped module defines and offers to others: global or weak, defined (not imports), visible
// (not hidden), functions and data (not sections, files or thread-local variables). The number of symbols
// comes from DT_GNU_HASH or DT_HASH.
[[nodiscard]] Result<std::vector<ExportedSymbol>> exportedSymbols(const Module& module);

// For ET_DYN modules, applies the relocations: R_X86_64_RELATIVE, R_X86_64_64, R_X86_64_GLOB_DAT and
// R_X86_64_JUMP_SLOT (PLT entries are bound eagerly). A symbol the module defines resolves to its own address;
// any other symbol is passed to `resolver`, which is called at most once per symbol. An unresolved weak import
// becomes 0. Does nothing for ET_EXEC modules. The module must still be writable (before protectModule).
[[nodiscard]] Status relocateModule(Module& module, const SymbolResolver& resolver);

// Sets the final protection of every segment.
[[nodiscard]] Status protectModule(Module& module, memory::AddressSpace& space);

// Unmaps every region of the module.
void unmapModule(Module& module, memory::AddressSpace& space);

// Maps, relocates and protects one image that will be run. Libraries and imports come from `options`. Not
// supported yet: other relocation types, DT_RELR, PT_GNU_RELRO. The entry point must lie in an executable segment.
//
// Either everything is mapped or nothing is: on failure, what was already mapped is unmapped.
[[nodiscard]] Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                          std::string_view name, const LoadOptions& options = {});

} // namespace zenith::loader
