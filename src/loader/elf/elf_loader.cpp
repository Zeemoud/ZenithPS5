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

bool insideImage(const LoadedImage& image, GuestAddr address, u64 size) {
    for (const LoadedSegment& segment : image.segments) {
        const u64 offset = address - segment.start;
        if (address >= segment.start && offset <= segment.size && size <= segment.size - offset) {
            return true;
        }
    }
    return false;
}

template <class T> T readFromImage(GuestAddr address) {
    T value;
    std::memcpy(&value, memory::AddressSpace::hostPointer(address), sizeof(value));
    return value;
}

// Reads the dynamic tables (strings, symbols) of a mapped image.
class ImageReader {
public:
    ImageReader(const LoadedImage& image, const DynamicInfo& dynamic, std::string_view name)
        : image_(image), dynamic_(dynamic), name_(name) {}

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

    Result<elf::Sym> symbolAt(u64 index) const {
        const GuestAddr entry = image_.base + dynamic_.symtab + index * sizeof(elf::Sym);
        if (dynamic_.symtab == 0 || !insideImage(image_, entry, sizeof(elf::Sym))) {
            return makeError("'{}': symbol {} is outside the symbol table", name_, index);
        }
        return readFromImage<elf::Sym>(entry);
    }

    // The number of entries of the symbol table, which the dynamic section does not give directly. It comes from
    // the hash table: DT_GNU_HASH (walk the chains to the last symbol) or DT_HASH (its chain count).
    Result<u64> symbolCount() const {
        if (dynamic_.symtab == 0) {
            return u64{0};
        }
        if (dynamic_.gnuHash != 0) {
            return gnuHashSymbolCount();
        }
        if (dynamic_.hash != 0) {
            const GuestAddr table = image_.base + dynamic_.hash;
            if (!insideImage(image_, table, 8)) {
                return makeError("'{}': the hash table is outside the image", name_);
            }
            return u64{readFromImage<u32>(table + 4)}; // nchain
        }
        return makeError("'{}': cannot tell how many symbols there are (no DT_GNU_HASH or DT_HASH)", name_);
    }

private:
    // Layout: u32 nbuckets, u32 symoffset, u32 bloomSize, u32 bloomShift, u64 bloom[bloomSize],
    // u32 buckets[nbuckets], u32 chains[] (one per symbol from symoffset on; bit 0 marks the end of a chain).
    Result<u64> gnuHashSymbolCount() const {
        const GuestAddr table = image_.base + dynamic_.gnuHash;
        if (!insideImage(image_, table, 16)) {
            return makeError("'{}': the hash table is outside the image", name_);
        }
        const u64 bucketCount = readFromImage<u32>(table);
        const u64 firstHashed = readFromImage<u32>(table + 4);
        const u64 bloomSize = readFromImage<u32>(table + 8);

        const GuestAddr buckets = table + 16 + bloomSize * sizeof(u64);
        if (!insideImage(image_, buckets, bucketCount * sizeof(u32))) {
            return makeError("'{}': the hash buckets are outside the image", name_);
        }
        u64 lastBucket = 0;
        for (u64 i = 0; i < bucketCount; ++i) {
            lastBucket = std::max<u64>(lastBucket, readFromImage<u32>(buckets + i * sizeof(u32)));
        }
        if (lastBucket < firstHashed) {
            return firstHashed; // no hashed symbol: only the ones before symoffset
        }

        const GuestAddr chains = buckets + bucketCount * sizeof(u32);
        u64 index = lastBucket;
        for (;;) {
            const GuestAddr link = chains + (index - firstHashed) * sizeof(u32);
            if (!insideImage(image_, link, sizeof(u32))) {
                return makeError("'{}': a hash chain leaves the image", name_);
            }
            if ((readFromImage<u32>(link) & 1U) != 0) {
                return index + 1;
            }
            ++index;
        }
    }

    const LoadedImage& image_;
    const DynamicInfo& dynamic_;
    std::string_view name_;
};

// Binds the relocations of an ET_DYN module that is already mapped (and still writable).
class Linker {
public:
    Linker(LoadedImage& image, const DynamicInfo& dynamic, const SymbolResolver& resolver, std::string_view name)
        : image_(image), reader_(image, dynamic, name), resolver_(resolver), name_(name) {}

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
            const auto relocation = readFromImage<elf::Rela>(tableAddress + i * sizeof(elf::Rela));

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
    // The address a symbol is bound to: its own address if the module defines it, else what the resolver says.
    Result<u64> symbolAddress(u32 index) {
        if (index == 0) {
            return u64{0};
        }
        if (const auto cached = resolved_.find(index); cached != resolved_.end()) {
            return cached->second;
        }

        const auto symbol = reader_.symbolAt(index);
        if (!symbol.ok()) {
            return makeError("{}", symbol.error());
        }

        u64 address = 0;
        if (symbol.value().shndx != elf::shnUndef) {
            address = symbol.value().shndx == elf::shnAbs ? symbol.value().value : image_.base + symbol.value().value;
        } else {
            const auto symbolName = reader_.stringAt(symbol.value().name);
            if (!symbolName.ok()) {
                return makeError("{}", symbolName.error());
            }
            const bool weak = (symbol.value().info >> 4) == elf::bindWeak;

            const auto bound = resolver_(symbolName.value());
            if (bound.ok()) {
                address = bound.value();
            } else if (!weak) {
                return makeError("'{}': cannot resolve the import '{}': {}", name_, symbolName.value(), bound.error());
            }
            image_.imports.push_back({std::string(symbolName.value()), address});
        }

        resolved_.emplace(index, address);
        return address;
    }

    LoadedImage& image_;
    ImageReader reader_;
    const SymbolResolver& resolver_;
    std::string_view name_;
    std::unordered_map<u32, u64> resolved_;
};

} // namespace

Result<Module> mapModule(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                         std::string_view name, GuestAddr baseAddress) {
    const bool relocatable = elf.type() == elf::typeSharedObject;
    const GuestAddr base = relocatable ? baseAddress : 0;

    if (base % memory::guestPageSize != 0) {
        return makeError("'{}': base address {:#x} is not aligned to {:#x}", name, base, memory::guestPageSize);
    }
    if (elf.dynamic().hasPackedRelocations) {
        return makeError("'{}': packed relocations (DT_RELR) are not supported yet", name);
    }

    // A loadable segment with its final guest addresses.
    struct Placement {
        const elf::ProgramHeader* header = nullptr;
        GuestAddr start = 0;       // exact address of the first byte of the segment
        GuestAddr regionStart = 0; // start rounded down to a guest page
        u64 regionSize = 0;        // whole guest pages
    };

    // Check everything that can be checked before touching the address space.
    std::vector<Placement> placements;
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
    }
    if (placements.empty()) {
        return makeError("'{}' has no loadable segment", name);
    }

    Module module(std::string(name), elf);
    module.image.base = base;
    module.image.entry = entry;

    // Map every segment writable and copy its contents.
    for (usize index = 0; index < placements.size(); ++index) {
        const Placement& placement = placements[index];
        const std::string regionName = std::format("{}:seg{}", name, index);

        if (!space.map(placement.regionStart, placement.regionSize, platform::readWrite, regionName)) {
            unmapModule(module, space);
            return makeError("'{}': cannot map segment {} at {:#x} + {:#x}", name, index, placement.regionStart,
                             placement.regionSize);
        }
        module.image.segments.push_back({placement.regionStart, placement.regionSize});
        module.plans.push_back({placement.regionStart, placement.regionSize, toProtection(placement.header->flags),
                                placement.start, placement.header->memsz});

        if (placement.header->filesz > 0) {
            std::memcpy(memory::AddressSpace::hostPointer(placement.start), data.data() + placement.header->offset,
                        placement.header->filesz);
        }
    }
    return module;
}

Status checkEntryPoint(const Module& module) {
    for (const SegmentPlan& plan : module.plans) {
        const bool executable = platform::hasFlag(plan.protection, platform::Protection::Execute);
        const bool containsEntry =
            module.image.entry >= plan.start && module.image.entry - plan.start < plan.memorySize;
        if (executable && containsEntry) {
            return success();
        }
    }
    return makeError("'{}': entry point {:#x} is not inside an executable segment", module.name, module.image.entry);
}

Result<std::vector<std::string>> neededLibraries(const Module& module) {
    const ImageReader reader(module.image, module.elf.dynamic(), module.name);

    std::vector<std::string> libraries;
    for (const u64 offset : module.elf.dynamic().needed) {
        const auto library = reader.stringAt(offset);
        if (!library.ok()) {
            return makeError("{}", library.error());
        }
        libraries.emplace_back(library.value());
    }
    return libraries;
}

Result<std::vector<ExportedSymbol>> exportedSymbols(const Module& module) {
    const ImageReader reader(module.image, module.elf.dynamic(), module.name);

    const auto count = reader.symbolCount();
    if (!count.ok()) {
        return makeError("{}", count.error());
    }

    std::vector<ExportedSymbol> exports;
    for (u64 index = 1; index < count.value(); ++index) {
        const auto symbol = reader.symbolAt(index);
        if (!symbol.ok()) {
            return makeError("{}", symbol.error());
        }
        const elf::Sym& entry = symbol.value();

        const u8 binding = entry.info >> 4;
        const u8 type = entry.info & 0xF;
        const u8 visibility = entry.other & 0x3;
        const bool defined = entry.shndx != elf::shnUndef && entry.name != 0;
        const bool global = binding == elf::bindGlobal || binding == elf::bindWeak;
        const bool usable = type != elf::symbolTypeSection && type != elf::symbolTypeFile && type != elf::symbolTypeTls;
        const bool visible = visibility != elf::visibilityInternal && visibility != elf::visibilityHidden;
        if (!defined || !global || !usable || !visible) {
            continue;
        }

        const auto name = reader.stringAt(entry.name);
        if (!name.ok()) {
            return makeError("{}", name.error());
        }
        const GuestAddr address = entry.shndx == elf::shnAbs ? entry.value : module.image.base + entry.value;
        exports.push_back({std::string(name.value()), address});
    }
    return exports;
}

Status relocateModule(Module& module, const SymbolResolver& resolver) {
    if (module.elf.type() != elf::typeSharedObject) {
        return success();
    }

    Linker linker(module.image, module.elf.dynamic(), resolver, module.name);
    const DynamicInfo& dynamic = module.elf.dynamic();
    for (const auto& [table, size] :
         {std::pair{dynamic.rela, dynamic.relaSize}, std::pair{dynamic.jmpRel, dynamic.pltRelSize}}) {
        const auto applied = linker.applyTable(table, size);
        if (!applied.ok()) {
            return makeError("{}", applied.error());
        }
        module.image.relocationCount += applied.value();
    }
    return success();
}

Status protectModule(Module& module, memory::AddressSpace& space) {
    for (usize index = 0; index < module.plans.size(); ++index) {
        const SegmentPlan& plan = module.plans[index];
        if (plan.protection != platform::readWrite &&
            !space.protect(plan.regionStart, plan.regionSize, plan.protection)) {
            return makeError("'{}': cannot set the protection of segment {}", module.name, index);
        }
    }

    ZENITH_LOG_INFO(Loader, "loaded '{}': {} segment(s), base {:#x}, entry {:#x}, {} relocation(s), {} import(s)",
                    module.name, module.image.segments.size(), module.image.base, module.image.entry,
                    module.image.relocationCount, module.image.imports.size());
    return success();
}

void unmapModule(Module& module, memory::AddressSpace& space) {
    for (const LoadedSegment& segment : module.image.segments) {
        static_cast<void>(space.unmap(segment.start, segment.size));
    }
    module.image.segments.clear();
    module.plans.clear();
}

Result<LoadedImage> loadElf(const ElfFile& elf, std::span<const u8> data, memory::AddressSpace& space,
                            std::string_view name, const LoadOptions& options) {
    auto mapped = mapModule(elf, data, space, name, options.baseAddress);
    if (!mapped.ok()) {
        return makeError("{}", mapped.error());
    }
    Module module = std::move(mapped).value();

    auto fail = [&module, &space](const std::string& message) -> Result<LoadedImage> {
        unmapModule(module, space);
        return makeError("{}", message);
    };

    if (const auto entryCheck = checkEntryPoint(module); !entryCheck.ok()) {
        return fail(entryCheck.error());
    }

    if (elf.type() == elf::typeSharedObject) {
        const auto libraries = neededLibraries(module);
        if (!libraries.ok()) {
            return fail(libraries.error());
        }
        for (const std::string& library : libraries.value()) {
            if (!options.hasLibrary || !options.hasLibrary(library)) {
                return fail(
                    std::format("'{}': the program needs the library '{}', which is not provided", name, library));
            }
            module.image.libraries.push_back(library);
        }

        const SymbolResolver resolver = [&options](std::string_view symbol) -> Result<GuestAddr> {
            if (!options.resolveSymbol) {
                return makeError("no import resolver was given");
            }
            return options.resolveSymbol(symbol);
        };
        const auto relocated = relocateModule(module, resolver);
        if (!relocated.ok()) {
            return fail(relocated.error());
        }
    }

    const auto protectedModule = protectModule(module, space);
    if (!protectedModule.ok()) {
        return fail(protectedModule.error());
    }
    return module.image;
}

} // namespace zenith::loader
