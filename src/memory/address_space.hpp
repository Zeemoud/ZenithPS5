// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"
#include "platform/virtual_memory.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace zenith::memory {

// Granularity of the guest memory API: addresses and sizes of every mapping are multiples of it.
// (16 KiB, as on the PS4; to be confirmed against the PS5 when the kernel HLE gets there.)
inline constexpr u64 guestPageSize = 16 * KiB;

struct Region {
    GuestAddr start = 0;
    u64 size = 0;
    platform::Protection protection = platform::Protection::None;
    std::string name;

    GuestAddr end() const { return start + size; }
    bool contains(GuestAddr address) const { return address >= start && address < end(); }
};

// The guest address space.
//
// Guest code runs natively on the host, so guest addresses are *identity-mapped*: guest address X
// is host address X. A mapping is therefore made at exactly the requested address, and fails if the
// host already uses any part of that range. Mappings are never silently replaced.
//
// Version 1 only supports whole-region operations (unmap/protect must name an existing region exactly).
// All methods are thread-safe.
class AddressSpace {
public:
    AddressSpace() = default;
    ~AddressSpace();

    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;
    AddressSpace(AddressSpace&&) = delete;
    AddressSpace& operator=(AddressSpace&&) = delete;

    // Maps [address, address + size) with the given protection. Memory starts zero-filled.
    [[nodiscard]] bool map(GuestAddr address, u64 size, platform::Protection protection, std::string_view name);

    // Unmaps an existing region (exact start and size).
    [[nodiscard]] bool unmap(GuestAddr address, u64 size);

    // Changes the protection of an existing region (exact start and size).
    [[nodiscard]] bool protect(GuestAddr address, u64 size, platform::Protection protection);

    // The region containing the address, if any.
    std::optional<Region> find(GuestAddr address) const;

    // True if every byte of [address, address + size) belongs to a mapped region.
    bool isMapped(GuestAddr address, u64 size) const;

    usize regionCount() const;

    // Identity mapping between guest and host addresses.
    static u8* hostPointer(GuestAddr address);
    static GuestAddr guestAddress(const void* pointer);

private:
    bool overlapsLocked(GuestAddr address, u64 size) const;

    mutable std::mutex mutex_;
    std::map<GuestAddr, Region> regions_;
};

} // namespace zenith::memory
