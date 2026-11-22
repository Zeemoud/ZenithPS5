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

inline constexpr u16 typeExecutable = 2;   // ET_EXEC: fixed addresses
inline constexpr u16 typeSharedObject = 3; // ET_DYN: position-independent, loaded at any base
inline constexpr u16 machineX86_64 = 62;   // EM_X86_64

inline constexpr u32 segmentLoad = 1;    // PT_LOAD
inline constexpr u32 segmentDynamic = 2; // PT_DYNAMIC

// Tags of the dynamic section (d_tag).
inline constexpr i64 dynNull = 0;
inline constexpr i64 dynNeeded = 1;
inline constexpr i64 dynPltRelSize = 2;
inline constexpr i64 dynStrtab = 5;
inline constexpr i64 dynSymtab = 6;
inline constexpr i64 dynRela = 7;
inline constexpr i64 dynRelaSize = 8;
inline constexpr i64 dynRelaEntry = 9;
inline constexpr i64 dynStrSize = 10;
inline constexpr i64 dynSymEntry = 11;
inline constexpr i64 dynJmpRel = 23;
inline constexpr i64 dynRelrSize = 35;
inline constexpr i64 dynRelr = 36;

// Relocation types (x86-64).
inline constexpr u32 relocNone = 0;     // R_X86_64_NONE
inline constexpr u32 reloc64 = 1;       // R_X86_64_64: S + A
inline constexpr u32 relocGlobDat = 6;  // R_X86_64_GLOB_DAT: S (a GOT entry)
inline constexpr u32 relocJumpSlot = 7; // R_X86_64_JUMP_SLOT: S (a PLT entry)
inline constexpr u32 relocRelative = 8; // R_X86_64_RELATIVE: B + A

inline constexpr u16 shnUndef = 0; // symbol defined elsewhere: an import
inline constexpr u8 bindWeak = 2;  // STB_WEAK, in the high nibble of Sym::info

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

struct Dyn {
    i64 tag;
    u64 value;
};

struct Rela {
    u64 offset;
    u64 info; // type in the low 32 bits, symbol index in the high 32 bits
    i64 addend;
};

struct Sym {
    u32 name; // offset in the string table
    u8 info;  // binding in the high nibble, type in the low nibble
    u8 other;
    u16 shndx; // shnUndef for an import
    u64 value;
    u64 size;
};

static_assert(sizeof(Sym) == 24, "ELF64 symbol must be 24 bytes");
static_assert(sizeof(Dyn) == 16, "ELF64 dynamic entry must be 16 bytes");
static_assert(sizeof(Rela) == 24, "ELF64 relocation entry must be 24 bytes");
static_assert(sizeof(Header) == 64, "ELF64 header must be 64 bytes");
static_assert(sizeof(ProgramHeader) == 56, "ELF64 program header must be 56 bytes");

} // namespace zenith::loader::elf
