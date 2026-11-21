// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "loader/elf/elf_file.hpp"
#include "memory/address_space.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace zenith::loader {

// A mapped region of the image: whole guest pages, starting at or before the first byte of a segment.
struct LoadedSegment {
    GuestAddr start = 0;
    u64 size = 0;
};

struct LoadedImage {
    GuestAddr base = 0; // load bias: 0 for ET_EXEC, the chosen base address for ET_DYN
    GuestAddr entry = 0;
    usize relocationCount = 0;
    std::vector<LoadedSegment> segments;
};

struct LoadOptions {
    // Where a position-independent image (ET_DYN) is loaded. Must be a multiple of the guest page size.
    // Ignored for fixed-address executables (ET_EXEC), which go where they were linked.
    GuestAddr baseAddress = 0x400000;
};

// Maps the PT_LOAD segments of `elf` into `space` and copies their contents (the part beyond the file data
// is zero-filled). `data` must be the bytes `elf` was parsed from.
//
// A segment does not need to start on a page boundary: the pages around it are mapped, and its bytes are put
// at their exact address. Two segments must not share a page (linkers keep them apart with
// -z max-page-size=0x4000), because each page has a single protection.
//
// For ET_DYN images, the R_X86_64_RELATIVE relocations are applied before the final protections are set.
// Not supported yet: other relocation types (imports), DT_NEEDED libraries, DT_RELR, PT_GNU_RELRO.
// The entry point must lie in an executable segment.
//
// Either everything is mapped or nothing is: on failure, what was already mapped is unmapped.
[[nodiscard]] Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                          std::string_view name, const LoadOptions& options = {});

} // namespace zenith::loader
