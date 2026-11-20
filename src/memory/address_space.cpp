// SPDX-License-Identifier: GPL-2.0-only
#include "memory/address_space.hpp"

#include <iterator>

#include "common/log.hpp"

namespace zenith::memory {
namespace {

// Upper bound of the canonical user-space range on x86-64.
constexpr GuestAddr userSpaceLimit = 1ULL << 47;

}  // namespace

AddressSpace::~AddressSpace() {
    const std::lock_guard lock(mutex_);
    for (const auto& [start, region] : regions_) {
        static_cast<void>(platform::release(hostPointer(start), region.size));
    }
}

u8* AddressSpace::hostPointer(GuestAddr address) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): guest addresses are host addresses by design
    return reinterpret_cast<u8*>(address);
}

GuestAddr AddressSpace::guestAddress(const void* pointer) {
    return reinterpret_cast<GuestAddr>(pointer);
}

bool AddressSpace::overlapsLocked(GuestAddr address, u64 size) const {
    const GuestAddr end = address + size;
    auto next = regions_.upper_bound(address);
    if (next != regions_.begin()) {
        const auto previous = std::prev(next);
        if (previous->second.end() > address) {
            return true;
        }
    }
    return next != regions_.end() && next->first < end;
}

bool AddressSpace::map(GuestAddr address, u64 size, platform::Protection protection, std::string_view name) {
    const bool aligned = address % guestPageSize == 0 && size % guestPageSize == 0;
    const bool inRange = size != 0 && address + size > address && address + size <= userSpaceLimit;
    if (!aligned || !inRange) {
        ZENITH_LOG_WARN(Memory, "map '{}' rejected: invalid range {:#x} + {:#x}", name, address, size);
        return false;
    }

    const std::lock_guard lock(mutex_);
    if (overlapsLocked(address, size)) {
        ZENITH_LOG_WARN(Memory, "map '{}' rejected: {:#x} + {:#x} overlaps an existing region", name, address, size);
        return false;
    }

    u8* host = hostPointer(address);
    if (!platform::reserveFixed(host, size)) {
        ZENITH_LOG_WARN(Memory, "map '{}' failed: host range {:#x} + {:#x} is not available", name, address, size);
        return false;
    }
    if (protection != platform::Protection::None && !platform::protect(host, size, protection)) {
        static_cast<void>(platform::release(host, size));
        ZENITH_LOG_WARN(Memory, "map '{}' failed: could not set protection", name);
        return false;
    }

    regions_.emplace(address, Region{address, size, protection, std::string(name)});
    ZENITH_LOG_DEBUG(Memory, "mapped '{}' at {:#x} + {:#x}", name, address, size);
    return true;
}

bool AddressSpace::unmap(GuestAddr address, u64 size) {
    const std::lock_guard lock(mutex_);
    const auto it = regions_.find(address);
    if (it == regions_.end() || it->second.size != size) {
        ZENITH_LOG_WARN(Memory, "unmap {:#x} + {:#x} rejected: no such region", address, size);
        return false;
    }
    if (!platform::release(hostPointer(address), size)) {
        return false;
    }
    ZENITH_LOG_DEBUG(Memory, "unmapped '{}' at {:#x}", it->second.name, address);
    regions_.erase(it);
    return true;
}

bool AddressSpace::protect(GuestAddr address, u64 size, platform::Protection protection) {
    const std::lock_guard lock(mutex_);
    const auto it = regions_.find(address);
    if (it == regions_.end() || it->second.size != size) {
        ZENITH_LOG_WARN(Memory, "protect {:#x} + {:#x} rejected: no such region", address, size);
        return false;
    }
    if (!platform::protect(hostPointer(address), size, protection)) {
        return false;
    }
    it->second.protection = protection;
    return true;
}

std::optional<Region> AddressSpace::find(GuestAddr address) const {
    const std::lock_guard lock(mutex_);
    auto next = regions_.upper_bound(address);
    if (next == regions_.begin()) {
        return std::nullopt;
    }
    const auto& region = std::prev(next)->second;
    if (!region.contains(address)) {
        return std::nullopt;
    }
    return region;
}

bool AddressSpace::isMapped(GuestAddr address, u64 size) const {
    if (size == 0 || address + size < address) {
        return false;
    }
    const std::lock_guard lock(mutex_);
    GuestAddr cursor = address;
    const GuestAddr end = address + size;

    auto it = regions_.upper_bound(cursor);
    if (it == regions_.begin()) {
        return false;
    }
    --it;
    while (cursor < end) {
        if (it == regions_.end() || !it->second.contains(cursor)) {
            return false;
        }
        cursor = it->second.end();
        ++it;
    }
    return true;
}

usize AddressSpace::regionCount() const {
    const std::lock_guard lock(mutex_);
    return regions_.size();
}

}  // namespace zenith::memory
