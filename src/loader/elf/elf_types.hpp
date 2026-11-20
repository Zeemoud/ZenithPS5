// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"

#include <array>
#include <bit>

// On-disk ELF64 structures (little-endian), defined here instead of using <elf.h> so the loader
// does not depend on the host OS headers.
namespace zenith::loader::elf {

static_assert(std::endian::native == std::endian::little, "the ELF reader assumes a little-endian host");

inline constexpr std::array<u8, 4> magic = {0x7F, 0x45, 0x4C, 0x46}; // "\x7f" "ELF"

// Indices into Header::ident.
inline constexpr usize identClass = 4;
inline constexpr usize identData = 5;
inline constexpr usize identVersion = 6;

inline constexpr u8 classElf64 = 2;
inline constexpr u8 dataLittleEndian = 1;
inline constexpr u8 currentVersion = 1;

inline constexpr u16 typeExecutable = 2; // ET_EXEC
inline constexpr u16 machineX86_64 = 62; // EM_X86_64

inline constexpr u32 segmentLoad = 1; // PT_LOAD

inline constexpr u32 flagExecute = 1;
inline constexpr u32 flagWrite = 2;
inline constexpr u32 flagRead = 4;

struct Header {
    std::array<u8, 16> ident;
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
};

struct ProgramHeader {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
};

static_assert(sizeof(Header) == 64, "ELF64 header must be 64 bytes");
static_assert(sizeof(ProgramHeader) == 56, "ELF64 program header must be 56 bytes");

} // namespace zenith::loader::elf
