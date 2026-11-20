// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"
#include "loader/elf/elf_types.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace zenith::test {

struct SegmentSpec {
    u32 type = loader::elf::segmentLoad;
    u32 flags = loader::elf::flagRead | loader::elf::flagExecute;
    u64 vaddr = 0;
    std::vector<u8> bytes;
    u64 memsize = 0; // 0 means bytes.size()
};

// Builds tiny synthetic ELF64 x86-64 executables, so tests need no real binary.
class ElfBuilder {
public:
    ElfBuilder& entry(u64 value) {
        entry_ = value;
        return *this;
    }

    ElfBuilder& segment(SegmentSpec spec) {
        segments_.push_back(std::move(spec));
        return *this;
    }

    std::vector<u8> build() const {
        using namespace loader::elf;

        Header header{};
        std::copy(magic.begin(), magic.end(), header.ident.begin());
        header.ident[identClass] = classElf64;
        header.ident[identData] = dataLittleEndian;
        header.ident[identVersion] = currentVersion;
        header.type = typeExecutable;
        header.machine = machineX86_64;
        header.version = currentVersion;
        header.entry = entry_;
        header.phoff = sizeof(Header);
        header.ehsize = sizeof(Header);
        header.phentsize = sizeof(ProgramHeader);
        header.phnum = static_cast<u16>(segments_.size());

        const u64 tableEnd = sizeof(Header) + segments_.size() * sizeof(ProgramHeader);
        const u64 dataStart = (tableEnd + 15) / 16 * 16;

        std::vector<ProgramHeader> programHeaders;
        std::vector<u8> payload;
        for (const SegmentSpec& spec : segments_) {
            ProgramHeader ph{};
            ph.type = spec.type;
            ph.flags = spec.flags;
            ph.offset = dataStart + payload.size();
            ph.vaddr = spec.vaddr;
            ph.paddr = spec.vaddr;
            ph.filesz = spec.bytes.size();
            ph.memsz = spec.memsize != 0 ? spec.memsize : spec.bytes.size();
            ph.align = 0x4000;
            programHeaders.push_back(ph);

            payload.insert(payload.end(), spec.bytes.begin(), spec.bytes.end());
            payload.resize((payload.size() + 15) / 16 * 16);
        }

        std::vector<u8> out(dataStart);
        std::memcpy(out.data(), &header, sizeof(header));
        if (!programHeaders.empty()) {
            std::memcpy(out.data() + sizeof(Header), programHeaders.data(),
                        programHeaders.size() * sizeof(ProgramHeader));
        }
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

private:
    u64 entry_ = 0;
    std::vector<SegmentSpec> segments_;
};

} // namespace zenith::test
