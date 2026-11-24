// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"
#include "loader/elf/elf_types.hpp"

#include <algorithm>
#include <cstring>
#include <string>
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
    u32 symbol = 0; // index in the symbol table (0 is the null symbol)
};

struct SymbolSpec {
    std::string name;
    bool defined = false; // false: an import
    u64 value = 0;        // link-time address, for a defined symbol
    bool weak = false;
    bool local = false;  // local binding: never exported
    bool hidden = false; // hidden visibility: never exported
};

// The hash table written in the dynamic segment: it tells the loader how many symbols there are.
enum class HashStyle { Sysv, Gnu, None };

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

    // Symbols (index 1, 2, ... in the symbol table) and DT_NEEDED libraries, stored in the dynamic segment.
    ElfBuilder& symbols(std::vector<SymbolSpec> specs) {
        symbols_ = std::move(specs);
        return *this;
    }

    ElfBuilder& hashStyle(HashStyle style) {
        hashStyle_ = style;
        return *this;
    }

    ElfBuilder& needed(std::vector<std::string> libraries) {
        needed_ = std::move(libraries);
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
            // String table and symbol table.
            std::vector<u8> strtab = {0};
            auto addString = [&strtab](const std::string& text) {
                const u32 offset = static_cast<u32>(strtab.size());
                strtab.insert(strtab.end(), text.begin(), text.end());
                strtab.push_back(0);
                return offset;
            };
            std::vector<u32> neededOffsets;
            for (const std::string& library : needed_) {
                neededOffsets.push_back(addString(library));
            }
            std::vector<Sym> symtab(1); // entry 0 is the null symbol
            for (const SymbolSpec& symbol : symbols_) {
                Sym entry{};
                entry.name = addString(symbol.name);
                const u8 binding = symbol.local ? 0 : (symbol.weak ? bindWeak : bindGlobal);
                entry.info = static_cast<u8>((binding << 4) | 2); // FUNC
                entry.other = symbol.hidden ? visibilityHidden : 0;
                entry.shndx = symbol.defined ? 1 : shnUndef;
                entry.value = symbol.value;
                symtab.push_back(entry);
            }
            const bool hasSymbols = !symbols_.empty() || !needed_.empty();

            // Hash table (only the symbol count matters to the loader: lookups scan the symbol table).
            std::vector<u32> hashWords;
            const u32 symbolTotal = static_cast<u32>(symtab.size());
            if (hashStyle_ == HashStyle::Sysv) {
                hashWords = {1, symbolTotal, 0};                  // nbucket, nchain, bucket[0]
                hashWords.resize(hashWords.size() + symbolTotal); // chain[nchain]
            } else if (hashStyle_ == HashStyle::Gnu) {
                const u32 hashed = symbolTotal - 1; // every symbol after the null one
                hashWords = {1,
                             1,
                             1,
                             6,
                             0xFFFFFFFFU,
                             0xFFFFFFFFU,                    // nbuckets, symoffset, bloom_size, shift, bloom[0]
                             hashed > 0 ? 1U : 0U};          // bucket[0]
                hashWords.resize(hashWords.size() + hashed); // chains
                if (hashed > 0) {
                    hashWords.back() |= 1U; // end of the chain
                }
            }
            const bool hasHash = hasSymbols && hashStyle_ != HashStyle::None;

            // Layout of the segment: dynamic section, relocation table, symbol table, string table.
            const u64 symbolTagCount = hasSymbols ? 4U : 0U;
            const u64 hashTagCount = hasHash ? 1U : 0U;
            const u64 tagCount = 3U + symbolTagCount + hashTagCount + neededOffsets.size() + extraTags_.size() + 1U;
            const u64 dynamicSize = tagCount * sizeof(Dyn);
            const u64 relaVaddr = dynamicVaddr_ + dynamicSize;
            const u64 symtabVaddr = relaVaddr + relocations_.size() * sizeof(Rela);
            const u64 strtabVaddr = symtabVaddr + symtab.size() * sizeof(Sym);
            const u64 hashVaddr = (strtabVaddr + strtab.size() + 7) / 8 * 8;

            std::vector<Dyn> entries;
            entries.push_back({dynRela, relaVaddr});
            entries.push_back({dynRelaSize, relocations_.size() * sizeof(Rela)});
            entries.push_back({dynRelaEntry, sizeof(Rela)});
            if (hasSymbols) {
                entries.push_back({dynSymtab, symtabVaddr});
                entries.push_back({dynStrtab, strtabVaddr});
                entries.push_back({dynStrSize, strtab.size()});
                entries.push_back({dynSymEntry, sizeof(Sym)});
            }
            if (hasHash) {
                entries.push_back({hashStyle_ == HashStyle::Sysv ? dynHash : dynGnuHash, hashVaddr});
            }
            for (const u32 offset : neededOffsets) {
                entries.push_back({dynNeeded, offset});
            }
            for (const auto& [tag, value] : extraTags_) {
                entries.push_back({tag, value});
            }
            entries.push_back({dynNull, 0});

            const u64 hashBytes = hasHash ? hashWords.size() * sizeof(u32) : 0;
            std::vector<u8> bytes(hashVaddr - dynamicVaddr_ + hashBytes);
            std::memcpy(bytes.data(), entries.data(), dynamicSize);
            for (usize i = 0; i < relocations_.size(); ++i) {
                const Rela rela{relocations_[i].offset,
                                (static_cast<u64>(relocations_[i].symbol) << 32) | relocations_[i].type,
                                relocations_[i].addend};
                std::memcpy(bytes.data() + dynamicSize + i * sizeof(Rela), &rela, sizeof(rela));
            }
            std::memcpy(bytes.data() + (symtabVaddr - dynamicVaddr_), symtab.data(), symtab.size() * sizeof(Sym));
            std::memcpy(bytes.data() + (strtabVaddr - dynamicVaddr_), strtab.data(), strtab.size());
            if (hasHash) {
                std::memcpy(bytes.data() + (hashVaddr - dynamicVaddr_), hashWords.data(), hashBytes);
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
    std::vector<SymbolSpec> symbols_;
    std::vector<std::string> needed_;
    HashStyle hashStyle_ = HashStyle::Sysv;
};

} // namespace zenith::test
