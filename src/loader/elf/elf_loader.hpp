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

struct LoadedSegment {
    GuestAddr start = 0;
    u64 size = 0;
};

struct LoadedImage {
    GuestAddr entry = 0;
    std::vector<LoadedSegment> segments;
};

// Maps the PT_LOAD segments of `elf` into `space` at their own virtual addresses and copies their
// contents (the part beyond the file data is zero-filled). `data` must be the bytes `elf` was parsed from.
//
// Version 1 limits: ET_EXEC only (no relocations), segment addresses must be multiples of the guest page
// size, and the entry point must lie in an executable segment.
//
// Either everything is mapped or nothing is: on failure, the segments already mapped are unmapped.
[[nodiscard]] Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                                          std::string_view name);

} // namespace zenith::loader
