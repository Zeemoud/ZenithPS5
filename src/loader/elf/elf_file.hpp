// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "loader/elf/elf_types.hpp"

#include <span>
#include <vector>

namespace zenith::loader {

// A validated ELF64 x86-64 executable (plain ELF, ET_EXEC only for now).
//
// parse() checks the headers and that every PT_LOAD segment lies inside the file, but it does not
// keep a reference to the data: the caller keeps the bytes alive for as long as it needs them.
class ElfFile {
public:
    [[nodiscard]] static Result<ElfFile> parse(std::span<const u8> data);

    u64 entry() const { return entry_; }
    const std::vector<elf::ProgramHeader>& programHeaders() const { return programHeaders_; }

private:
    ElfFile() = default;

    u64 entry_ = 0;
    std::vector<elf::ProgramHeader> programHeaders_;
};

} // namespace zenith::loader
