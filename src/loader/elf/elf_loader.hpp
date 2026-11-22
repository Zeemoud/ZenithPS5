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
    std::function<Result<GuestAddr>(std::string_view)> resolveSymbol;
};

// Maps the PT_LOAD segments of `elf` into `space` and copies their contents (the part beyond the file data
// is zero-filled). `data` must be the bytes `elf` was parsed from.
//
// A segment does not need to start on a page boundary: the pages around it are mapped, and its bytes are put
// at their exact address. Two segments must not share a page (linkers keep them apart with
// -z max-page-size=0x4000), because each page has a single protection.
//
// For ET_DYN images, relocations are applied before the final protections are set: R_X86_64_RELATIVE,
// R_X86_64_64, R_X86_64_GLOB_DAT and R_X86_64_JUMP_SLOT (PLT entries are bound eagerly). Imported symbols
// are resolved by name through LoadOptions. A symbol defined by the image itself resolves to its own address;
// an unresolved weak import becomes 0.
// Not supported yet: other relocation types, DT_RELR, PT_GNU_RELRO, symbols exported to other modules.
// The entry point must lie in an executable segment.
//
// Either everything is mapped or nothing is: on failure, what was already mapped is unmapped.
[[nodiscard]] Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                          std::string_view name, const LoadOptions& options = {});

} // namespace zenith::loader
