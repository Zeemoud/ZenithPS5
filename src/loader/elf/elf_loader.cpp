// SPDX-License-Identifier: GPL-2.0-only
#include "loader/elf/elf_loader.hpp"

#include "common/log.hpp"

#include <cstring>
#include <format>

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
        if (address >= segment.start && address - segment.start <= segment.size &&
            size <= segment.size - (address - segment.start)) {
            return true;
        }
    }
    return false;
}

// Applies the entries of one relocation table (Elf64_Rela). The table and every target must be inside the image.
Result<usize> applyRelocationTable(const LoadedImage& image, GuestAddr table, u64 tableSize, std::string_view name) {
    if (tableSize == 0) {
        return usize{0};
    }
    const GuestAddr tableAddress = image.base + table;
    if (!insideImage(image, tableAddress, tableSize)) {
        return makeError("'{}': a relocation table is outside the image", name);
    }

    usize applied = 0;
    for (u64 i = 0; i < tableSize / sizeof(elf::Rela); ++i) {
        elf::Rela relocation;
        std::memcpy(&relocation, memory::AddressSpace::hostPointer(tableAddress + i * sizeof(elf::Rela)),
                    sizeof(relocation));

        const u32 type = static_cast<u32>(relocation.info & 0xFFFFFFFFU);
        if (type == elf::relocNone) {
            continue;
        }
        if (type != elf::relocRelative) {
            return makeError("'{}': relocation type {} at {:#x} is not supported yet (only R_X86_64_RELATIVE)", name,
                             type, relocation.offset);
        }

        const GuestAddr target = image.base + relocation.offset;
        if (!insideImage(image, target, sizeof(u64))) {
            return makeError("'{}': relocation target {:#x} is outside the image", name, relocation.offset);
        }
        const u64 value = image.base + static_cast<u64>(relocation.addend);
        std::memcpy(memory::AddressSpace::hostPointer(target), &value, sizeof(value));
        ++applied;
    }
    return applied;
}

} // namespace

Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                            std::string_view name, const LoadOptions& options) {
    const bool relocatable = elf.type() == elf::typeSharedObject;
    const GuestAddr base = relocatable ? options.baseAddress : 0;

    if (base % memory::guestPageSize != 0) {
        return makeError("'{}': base address {:#x} is not aligned to {:#x}", name, base, memory::guestPageSize);
    }
    if (elf.dynamic().needsLibraries) {
        return makeError("'{}': the program needs shared libraries (not supported yet)", name);
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

    // Pass 2: relocations, while everything is still writable (the targets may be in read-only segments).
    if (relocatable) {
        for (const auto& [table, size] : {std::pair{elf.dynamic().rela, elf.dynamic().relaSize},
                                          std::pair{elf.dynamic().jmpRel, elf.dynamic().pltRelSize}}) {
            const auto applied = applyRelocationTable(image, table, size, name);
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

    ZENITH_LOG_INFO(Loader, "loaded '{}': {} segment(s), base {:#x}, entry {:#x}, {} relocation(s)", name,
                    image.segments.size(), image.base, image.entry, image.relocationCount);
    return image;
}

} // namespace zenith::loader
