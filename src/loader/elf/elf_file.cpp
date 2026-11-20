// SPDX-License-Identifier: GPL-2.0-only
#include "loader/elf/elf_file.hpp"

#include <algorithm>
#include <cstring>

namespace zenith::loader {
namespace {

constexpr u16 maxProgramHeaders = 256;

// True if [offset, offset + size) lies inside a buffer of `total` bytes (overflow-safe).
constexpr bool fitsIn(u64 offset, u64 size, u64 total) {
    return offset <= total && size <= total - offset;
}

} // namespace

Result<ElfFile> ElfFile::parse(std::span<const u8> data) {
    if (data.size() < sizeof(elf::Header)) {
        return makeError("file too small for an ELF header ({} bytes)", data.size());
    }

    elf::Header header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (!std::equal(elf::magic.begin(), elf::magic.end(), header.ident.begin())) {
        return makeError("not an ELF file (bad magic)");
    }
    if (header.ident[elf::identClass] != elf::classElf64) {
        return makeError("not a 64-bit ELF file");
    }
    if (header.ident[elf::identData] != elf::dataLittleEndian) {
        return makeError("not a little-endian ELF file");
    }
    if (header.ident[elf::identVersion] != elf::currentVersion || header.version != elf::currentVersion) {
        return makeError("unsupported ELF version");
    }
    if (header.machine != elf::machineX86_64) {
        return makeError("unsupported machine {:#x} (expected x86-64)", header.machine);
    }
    if (header.type != elf::typeExecutable) {
        return makeError("unsupported ELF type {:#x} (only ET_EXEC is supported yet)", header.type);
    }
    if (header.phentsize != sizeof(elf::ProgramHeader)) {
        return makeError("unexpected program header size {}", header.phentsize);
    }
    if (header.phnum == 0 || header.phnum > maxProgramHeaders) {
        return makeError("invalid program header count {}", header.phnum);
    }

    const u64 tableSize = static_cast<u64>(header.phnum) * header.phentsize;
    if (!fitsIn(header.phoff, tableSize, data.size())) {
        return makeError("program header table is outside the file");
    }

    ElfFile file;
    file.entry_ = header.entry;
    file.programHeaders_.resize(header.phnum);
    std::memcpy(file.programHeaders_.data(), data.data() + header.phoff, tableSize);

    for (usize i = 0; i < file.programHeaders_.size(); ++i) {
        const elf::ProgramHeader& ph = file.programHeaders_[i];
        if (ph.type != elf::segmentLoad) {
            continue;
        }
        if (ph.filesz > ph.memsz) {
            return makeError("segment {}: file size {:#x} is larger than memory size {:#x}", i, ph.filesz, ph.memsz);
        }
        if (!fitsIn(ph.offset, ph.filesz, data.size())) {
            return makeError("segment {}: data is outside the file", i);
        }
        if (ph.vaddr + ph.memsz < ph.vaddr) {
            return makeError("segment {}: address range overflows", i);
        }
    }

    return file;
}

} // namespace zenith::loader
