// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "loader/elf/elf_types.hpp"

#include <span>
#include <vector>

namespace zenith::loader {

// What the dynamic section (PT_DYNAMIC) says about relocations and dependencies.
// Addresses are link-time virtual addresses, relative to the load base.
struct DynamicInfo {
    u64 rela = 0;     // DT_RELA: table of relocations with addends
    u64 relaSize = 0; // DT_RELASZ: size in bytes
    u64 jmpRel = 0;   // DT_JMPREL: relocations of the procedure linkage table (same entry format)
    u64 pltRelSize = 0;
    u64 symtab = 0;                    // DT_SYMTAB: table of symbols (Elf64_Sym)
    u64 strtab = 0;                    // DT_STRTAB: string table
    u64 strSize = 0;                   // DT_STRSZ
    u64 hash = 0;                      // DT_HASH: classic hash table (its chain count is the number of symbols)
    u64 gnuHash = 0;                   // DT_GNU_HASH: the hash table linkers produce by default
    std::vector<u64> needed;           // DT_NEEDED: offsets, in the string table, of the library names
    bool hasPackedRelocations = false; // DT_RELR: compact relocation format
};

// A validated ELF64 x86-64 file: a fixed-address executable (ET_EXEC) or a position-independent one
// (ET_DYN, with its dynamic section).
//
// parse() checks the headers, that every PT_LOAD segment lies inside the file, and the dynamic section, but
// it does not keep a reference to the data: the caller keeps the bytes alive for as long as it needs them.
class ElfFile {
public:
    [[nodiscard]] static Result<ElfFile> parse(std::span<const u8> data);

    u16 type() const { return type_; }
    u64 entry() const { return entry_; }
    const std::vector<elf::ProgramHeader>& programHeaders() const { return programHeaders_; }

    // Empty (all zero) for files without a dynamic section.
    const DynamicInfo& dynamic() const { return dynamic_; }

private:
    ElfFile() = default;

    u16 type_ = 0;
    u64 entry_ = 0;
    std::vector<elf::ProgramHeader> programHeaders_;
    DynamicInfo dynamic_;
};

} // namespace zenith::loader
