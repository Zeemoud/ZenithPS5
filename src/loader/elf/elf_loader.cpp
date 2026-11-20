// SPDX-License-Identifier: GPL-2.0-only
#include "loader/elf/elf_loader.hpp"

#include "common/log.hpp"

#include <cstring>
#include <format>

namespace zenith::loader {
namespace {

// Upper bound of the canonical user-space range on x86-64.
constexpr u64 userSpaceLimit = 1ULL << 47;

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

} // namespace

Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                            std::string_view name) {
    std::vector<const elf::ProgramHeader*> segments;
    for (const elf::ProgramHeader& ph : elf.programHeaders()) {
        if (ph.type == elf::segmentLoad && ph.memsz != 0) {
            segments.push_back(&ph);
        }
    }
    if (segments.empty()) {
        return makeError("'{}' has no loadable segment", name);
    }

    // Check everything that can be checked before touching the address space.
    bool entryIsExecutable = false;
    for (const elf::ProgramHeader* ph : segments) {
        if (ph->vaddr % memory::guestPageSize != 0) {
            return makeError("'{}': segment address {:#x} is not aligned to {:#x} (not supported yet)", name, ph->vaddr,
                             memory::guestPageSize);
        }
        if (ph->memsz > userSpaceLimit || ph->vaddr > userSpaceLimit - ph->memsz) {
            return makeError("'{}': segment {:#x} + {:#x} is outside the user address range", name, ph->vaddr,
                             ph->memsz);
        }
        if (ph->offset > data.size() || ph->filesz > data.size() - ph->offset) {
            return makeError("'{}': segment data is outside the provided file data", name);
        }
        const bool containsEntry = elf.entry() >= ph->vaddr && elf.entry() - ph->vaddr < ph->memsz;
        if (containsEntry && (ph->flags & elf::flagExecute) != 0) {
            entryIsExecutable = true;
        }
    }
    if (!entryIsExecutable) {
        return makeError("'{}': entry point {:#x} is not inside an executable segment", name, elf.entry());
    }

    LoadedImage image;
    image.entry = elf.entry();

    auto rollback = [&image, &space] {
        for (const LoadedSegment& segment : image.segments) {
            static_cast<void>(space.unmap(segment.start, segment.size));
        }
        image.segments.clear();
    };

    for (usize index = 0; index < segments.size(); ++index) {
        const elf::ProgramHeader& ph = *segments[index];
        const u64 size = alignUp(ph.memsz, memory::guestPageSize);
        const std::string regionName = std::format("{}:seg{}", name, index);

        // Map writable first so the contents can be copied, then apply the final protection.
        if (!space.map(ph.vaddr, size, platform::readWrite, regionName)) {
            rollback();
            return makeError("'{}': cannot map segment {} at {:#x} + {:#x}", name, index, ph.vaddr, size);
        }
        image.segments.push_back({ph.vaddr, size});

        if (ph.filesz > 0) {
            std::memcpy(memory::AddressSpace::hostPointer(ph.vaddr), data.data() + ph.offset, ph.filesz);
        }

        const platform::Protection protection = toProtection(ph.flags);
        if (protection != platform::readWrite && !space.protect(ph.vaddr, size, protection)) {
            rollback();
            return makeError("'{}': cannot set the protection of segment {}", name, index);
        }
    }

    ZENITH_LOG_INFO(Loader, "loaded '{}': {} segment(s), entry {:#x}", name, image.segments.size(), image.entry);
    return image;
}

} // namespace zenith::loader
