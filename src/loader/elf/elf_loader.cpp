// SPDX-License-Identifier: GPL-2.0-only
#include "loader/elf/elf_loader.hpp"

#include "common/log.hpp"

#include <cstring>
#include <format>
#include <unordered_map>

namespace zenith::loader {
namespace {

// Upper bound of the canonical user-space range on x86-64.
constexpr u64 userSpaceLimit = 1ULL << 47;

constexpr u64 alignDown(u64 value, u64 alignment) {
    return value / alignment * alignment;
}

constexpr u64 alignUp(u64 value, u64 alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

platform::Protection toProtection(u32 flags) {
    platform::Protection protection = platform::Protection::None;
    if ((flags & elf::flagRead) != 0) {
        protection = protection | platform::Protection::Read;
    }
    if ((flags & elf::flagWrite) != 0) {
        protection = protection | platform::Protection::Write;
    }
    if ((flags & elf::flagExecute) != 0) {
        protection = protection | platform::Protection::Execute;
    }
    return protection;
}

// A loadable segment with its final guest addresses.
struct Placement {
    const elf::ProgramHeader* header = nullptr;
    GuestAddr start = 0;       // exact address of the first byte of the segment
    GuestAddr regionStart = 0; // start rounded down to a guest page
    u64 regionSize = 0;        // whole guest pages
};

bool insideImage(const LoadedImage& image, GuestAddr address, u64 size) {
    for (const LoadedSegment& segment : image.segments) {
        const u64 offset = address - segment.start;
        if (address >= segment.start && offset <= segment.size && size <= segment.size - offset) {
            return true;
        }
    }
    return false;
}

// Binds the relocations of an ET_DYN image that is already mapped (and still writable).
class Linker {
public:
    Linker(LoadedImage& image, const DynamicInfo& dynamic, const LoadOptions& options, std::string_view name)
        : image_(image), dynamic_(dynamic), options_(options), name_(name) {}

    // Every DT_NEEDED library must be provided by the host.
    Result<usize> checkLibraries() {
        for (const u64 offset : dynamic_.needed) {
            const auto library = stringAt(offset);
            if (!library.ok()) {
                return makeError("{}", library.error());
            }
            if (!options_.hasLibrary || !options_.hasLibrary(library.value())) {
                return makeError("'{}': the program needs the library '{}', which is not provided", name_,
                                 library.value());
            }
            image_.libraries.emplace_back(library.value());
        }
        return image_.libraries.size();
    }

    // Applies the entries of one relocation table (Elf64_Rela). The table and every target must be inside the image.
    Result<usize> applyTable(GuestAddr table, u64 tableSize) {
        if (tableSize == 0) {
            return usize{0};
        }
        const GuestAddr tableAddress = image_.base + table;
        if (!insideImage(image_, tableAddress, tableSize)) {
            return makeError("'{}': a relocation table is outside the image", name_);
        }

        usize applied = 0;
        for (u64 i = 0; i < tableSize / sizeof(elf::Rela); ++i) {
            elf::Rela relocation;
            std::memcpy(&relocation, memory::AddressSpace::hostPointer(tableAddress + i * sizeof(elf::Rela)),
                        sizeof(relocation));

            const u32 type = static_cast<u32>(relocation.info & 0xFFFFFFFFU);
            const u32 symbol = static_cast<u32>(relocation.info >> 32);
            if (type == elf::relocNone) {
                continue;
            }

            u64 value = 0;
            switch (type) {
            case elf::relocRelative:
                value = image_.base + static_cast<u64>(relocation.addend);
                break;
            case elf::reloc64: {
                const auto address = symbolAddress(symbol);
                if (!address.ok()) {
                    return makeError("{}", address.error());
                }
                value = address.value() + static_cast<u64>(relocation.addend);
                break;
            }
            case elf::relocGlobDat:
            case elf::relocJumpSlot: {
                if (symbol == 0) {
                    return makeError("'{}': relocation type {} at {:#x} has no symbol", name_, type, relocation.offset);
                }
                const auto address = symbolAddress(symbol);
                if (!address.ok()) {
                    return makeError("{}", address.error());
                }
                value = address.value();
                break;
            }
            default:
                return makeError("'{}': relocation type {} at {:#x} is not supported yet", name_, type,
                                 relocation.offset);
            }

            const GuestAddr target = image_.base + relocation.offset;
            if (!insideImage(image_, target, sizeof(u64))) {
                return makeError("'{}': relocation target {:#x} is outside the image", name_, relocation.offset);
            }
            std::memcpy(memory::AddressSpace::hostPointer(target), &value, sizeof(value));
            ++applied;
        }
        return applied;
    }

private:
    // A NUL-terminated string of the string table. The view points into guest memory.
    Result<std::string_view> stringAt(u64 offset) const {
        if (dynamic_.strtab == 0 || dynamic_.strSize == 0) {
            return makeError("'{}': the program has no string table", name_);
        }
        const GuestAddr table = image_.base + dynamic_.strtab;
        if (!insideImage(image_, table, dynamic_.strSize) || offset >= dynamic_.strSize) {
            return makeError("'{}': a name is outside the string table", name_);
        }

        const char* text = reinterpret_cast<const char*>(memory::AddressSpace::hostPointer(table + offset));
        const u64 room = dynamic_.strSize - offset;
        const usize length = strnlen(text, room);
        if (length == room) {
            return makeError("'{}': a name in the string table is not terminated", name_);
        }
        return std::string_view(text, length);
    }

    // The address a symbol is bound to: its own address if the image defines it, else what the host provides.
    Result<u64> symbolAddress(u32 index) {
        if (index == 0) {
            return u64{0};
        }
        if (const auto cached = resolved_.find(index); cached != resolved_.end()) {
            return cached->second;
        }

        const GuestAddr entry = image_.base + dynamic_.symtab + static_cast<u64>(index) * sizeof(elf::Sym);
        if (dynamic_.symtab == 0 || !insideImage(image_, entry, sizeof(elf::Sym))) {
            return makeError("'{}': symbol {} is outside the symbol table", name_, index);
        }
        elf::Sym symbol;
        std::memcpy(&symbol, memory::AddressSpace::hostPointer(entry), sizeof(symbol));

        u64 address = 0;
        if (symbol.shndx != elf::shnUndef) {
            address = image_.base + symbol.value;
        } else {
            const auto symbolName = stringAt(symbol.name);
            if (!symbolName.ok()) {
                return makeError("{}", symbolName.error());
            }
            const bool weak = (symbol.info >> 4) == elf::bindWeak;

            if (!options_.resolveSymbol) {
                if (!weak) {
                    return makeError("'{}': the program imports '{}' but no import resolver was given", name_,
                                     symbolName.value());
                }
            } else {
                const auto bound = options_.resolveSymbol(symbolName.value());
                if (bound.ok()) {
                    address = bound.value();
                } else if (!weak) {
                    return makeError("'{}': cannot resolve the import '{}': {}", name_, symbolName.value(),
                                     bound.error());
                }
            }
            image_.imports.push_back({std::string(symbolName.value()), address});
        }

        resolved_.emplace(index, address);
        return address;
    }

    LoadedImage& image_;
    const DynamicInfo& dynamic_;
    const LoadOptions& options_;
    std::string_view name_;
    std::unordered_map<u32, u64> resolved_;
};

} // namespace

Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                            std::string_view name, const LoadOptions& options) {
    const bool relocatable = elf.type() == elf::typeSharedObject;
    const GuestAddr base = relocatable ? options.baseAddress : 0;

    if (base % memory::guestPageSize != 0) {
        return makeError("'{}': base address {:#x} is not aligned to {:#x}", name, base, memory::guestPageSize);
    }
    if (elf.dynamic().hasPackedRelocations) {
        return makeError("'{}': packed relocations (DT_RELR) are not supported yet", name);
    }

    // Check everything that can be checked before touching the address space.
    std::vector<Placement> placements;
    bool entryIsExecutable = false;
    const GuestAddr entry = base + elf.entry();

    for (const elf::ProgramHeader& ph : elf.programHeaders()) {
        if (ph.type != elf::segmentLoad || ph.memsz == 0) {
            continue;
        }
        if (base > userSpaceLimit || ph.vaddr > userSpaceLimit - base || ph.memsz > userSpaceLimit - base - ph.vaddr) {
            return makeError("'{}': segment {:#x} + {:#x} is outside the user address range", name, base + ph.vaddr,
                             ph.memsz);
        }
        if (ph.offset > data.size() || ph.filesz > data.size() - ph.offset) {
            return makeError("'{}': segment data is outside the provided file data", name);
        }

        Placement placement;
        placement.header = &ph;
        placement.start = base + ph.vaddr;
        placement.regionStart = alignDown(placement.start, memory::guestPageSize);
        placement.regionSize = alignUp(placement.start + ph.memsz, memory::guestPageSize) - placement.regionStart;
        placements.push_back(placement);

        const bool containsEntry = entry >= placement.start && entry - placement.start < ph.memsz;
        if (containsEntry && (ph.flags & elf::flagExecute) != 0) {
            entryIsExecutable = true;
        }
    }
    if (placements.empty()) {
        return makeError("'{}' has no loadable segment", name);
    }
    if (!entryIsExecutable) {
        return makeError("'{}': entry point {:#x} is not inside an executable segment", name, entry);
    }

    LoadedImage image;
    image.base = base;
    image.entry = entry;

    auto rollback = [&image, &space] {
        for (const LoadedSegment& segment : image.segments) {
            static_cast<void>(space.unmap(segment.start, segment.size));
        }
        image.segments.clear();
    };

    // Pass 1: map every segment writable and copy its contents.
    for (usize index = 0; index < placements.size(); ++index) {
        const Placement& placement = placements[index];
        const std::string regionName = std::format("{}:seg{}", name, index);

        if (!space.map(placement.regionStart, placement.regionSize, platform::readWrite, regionName)) {
            rollback();
            return makeError("'{}': cannot map segment {} at {:#x} + {:#x}", name, index, placement.regionStart,
                             placement.regionSize);
        }
        image.segments.push_back({placement.regionStart, placement.regionSize});

        if (placement.header->filesz > 0) {
            std::memcpy(memory::AddressSpace::hostPointer(placement.start), data.data() + placement.header->offset,
                        placement.header->filesz);
        }
    }

    // Pass 2: libraries and relocations, while everything is still writable (the targets may be in
    // segments that become read-only).
    if (relocatable) {
        Linker linker(image, elf.dynamic(), options, name);

        const auto libraries = linker.checkLibraries();
        if (!libraries.ok()) {
            rollback();
            return makeError("{}", libraries.error());
        }
        for (const auto& [table, size] : {std::pair{elf.dynamic().rela, elf.dynamic().relaSize},
                                          std::pair{elf.dynamic().jmpRel, elf.dynamic().pltRelSize}}) {
            const auto applied = linker.applyTable(table, size);
            if (!applied.ok()) {
                rollback();
                return makeError("{}", applied.error());
            }
            image.relocationCount += applied.value();
        }
    }

    // Pass 3: final protections.
    for (usize index = 0; index < placements.size(); ++index) {
        const Placement& placement = placements[index];
        const platform::Protection protection = toProtection(placement.header->flags);
        if (protection != platform::readWrite &&
            !space.protect(placement.regionStart, placement.regionSize, protection)) {
            rollback();
            return makeError("'{}': cannot set the protection of segment {}", name, index);
        }
    }

    ZENITH_LOG_INFO(Loader, "loaded '{}': {} segment(s), base {:#x}, entry {:#x}, {} relocation(s), {} import(s)", name,
                    image.segments.size(), image.base, image.entry, image.relocationCount, image.imports.size());
    return image;
}

} // namespace zenith::loader
