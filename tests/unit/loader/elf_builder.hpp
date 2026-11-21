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

struct RelocationSpec {
    u64 offset = 0;
    u32 type = loader::elf::relocRelative;
    i64 addend = 0;
};

// Builds tiny synthetic ELF64 x86-64 files, so tests need no real binary.
class ElfBuilder {
public:
    ElfBuilder& entry(u64 value) {
        entry_ = value;
        return *this;
    }

    ElfBuilder& type(u16 value) {
        type_ = value;
        return *this;
    }

    ElfBuilder& segment(SegmentSpec spec) {
        segments_.push_back(std::move(spec));
        return *this;
    }

    // Adds a read-only segment at `vaddr` holding a dynamic section (DT_RELA/RELASZ/RELAENT, then the
    // `extraTags`, then DT_NULL) followed by the relocation table, plus the PT_DYNAMIC header that points to it.
    ElfBuilder& dynamic(u64 vaddr, std::vector<RelocationSpec> relocations,
                        std::vector<std::pair<i64, u64>> extraTags = {}) {
        hasDynamic_ = true;
        dynamicVaddr_ = vaddr;
        relocations_ = std::move(relocations);
        extraTags_ = std::move(extraTags);
        return *this;
    }

    std::vector<u8> build() const {
        using namespace loader::elf;

        const usize programHeaderCount = segments_.size() + (hasDynamic_ ? 2 : 0);

        Header header{};
        std::copy(magic.begin(), magic.end(), header.ident.begin());
        header.ident[identClass] = classElf64;
        header.ident[identData] = dataLittleEndian;
        header.ident[identVersion] = currentVersion;
        header.type = type_;
        header.machine = machineX86_64;
        header.version = currentVersion;
        header.entry = entry_;
        header.phoff = sizeof(Header);
        header.ehsize = sizeof(Header);
        header.phentsize = sizeof(ProgramHeader);
        header.phnum = static_cast<u16>(programHeaderCount);

        const u64 tableEnd = sizeof(Header) + programHeaderCount * sizeof(ProgramHeader);
        const u64 dataStart = (tableEnd + 15) / 16 * 16;

        std::vector<ProgramHeader> programHeaders;
        std::vector<u8> payload;

        auto addSegment = [&](u32 segmentType, u32 flags, u64 vaddr, const std::vector<u8>& bytes, u64 memsize) {
            ProgramHeader ph{};
            ph.type = segmentType;
            ph.flags = flags;
            ph.offset = dataStart + payload.size();
            ph.vaddr = vaddr;
            ph.paddr = vaddr;
            ph.filesz = bytes.size();
            ph.memsz = memsize != 0 ? memsize : bytes.size();
            ph.align = 0x4000;
            programHeaders.push_back(ph);

            payload.insert(payload.end(), bytes.begin(), bytes.end());
            payload.resize((payload.size() + 15) / 16 * 16);
        };

        for (const SegmentSpec& spec : segments_) {
            addSegment(spec.type, spec.flags, spec.vaddr, spec.bytes, spec.memsize);
        }

        if (hasDynamic_) {
            std::vector<Dyn> entries;
            const u64 dynamicSize = (4 + extraTags_.size()) * sizeof(Dyn); // RELA, RELASZ, RELAENT, extras, NULL
            entries.push_back({dynRela, dynamicVaddr_ + dynamicSize});
            entries.push_back({dynRelaSize, relocations_.size() * sizeof(Rela)});
            entries.push_back({dynRelaEntry, sizeof(Rela)});
            for (const auto& [tag, value] : extraTags_) {
                entries.push_back({tag, value});
            }
            entries.push_back({dynNull, 0});

            std::vector<u8> bytes(dynamicSize + relocations_.size() * sizeof(Rela));
            std::memcpy(bytes.data(), entries.data(), dynamicSize);
            for (usize i = 0; i < relocations_.size(); ++i) {
                const Rela rela{relocations_[i].offset, relocations_[i].type, relocations_[i].addend};
                std::memcpy(bytes.data() + dynamicSize + i * sizeof(Rela), &rela, sizeof(rela));
            }

            // The LOAD segment that holds the section, then the PT_DYNAMIC header pointing into it.
            const u64 offset = dataStart + payload.size();
            addSegment(segmentLoad, flagRead, dynamicVaddr_, bytes, 0);
            ProgramHeader dynamicHeader{};
            dynamicHeader.type = segmentDynamic;
            dynamicHeader.flags = flagRead | flagWrite;
            dynamicHeader.offset = offset;
            dynamicHeader.vaddr = dynamicVaddr_;
            dynamicHeader.paddr = dynamicVaddr_;
            dynamicHeader.filesz = dynamicSize;
            dynamicHeader.memsz = dynamicSize;
            dynamicHeader.align = 8;
            programHeaders.push_back(dynamicHeader);
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
    u16 type_ = loader::elf::typeExecutable;
    u64 entry_ = 0;
    std::vector<SegmentSpec> segments_;

    bool hasDynamic_ = false;
    u64 dynamicVaddr_ = 0;
    std::vector<RelocationSpec> relocations_;
    std::vector<std::pair<i64, u64>> extraTags_;
};

} // namespace zenith::test
