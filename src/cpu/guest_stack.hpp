// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "memory/address_space.hpp"

#include <string_view>

namespace zenith::cpu {

// Size of the inaccessible guard area at the bottom of every guest stack.
inline constexpr u64 stackGuardSize = memory::guestPageSize;

// A stack for guest code, mapped in the guest address space.
//
// Layout of [base, base + size): the lowest `stackGuardSize` bytes are inaccessible (an overflow
// faults instead of silently corrupting memory), the rest is read/write. The stack grows down from top().
// The mappings are released when the object is destroyed, so it must not outlive its AddressSpace.
class GuestStack {
public:
    // `size` must be a multiple of the guest page size and larger than the guard area.
    [[nodiscard]] static Result<GuestStack> create(memory::AddressSpace& space, GuestAddr base, u64 size,
                                                   std::string_view name);

    ~GuestStack();

    GuestStack(const GuestStack&) = delete;
    GuestStack& operator=(const GuestStack&) = delete;
    GuestStack(GuestStack&& other) noexcept;
    GuestStack& operator=(GuestStack&& other) noexcept;

    memory::AddressSpace& space() const { return *space_; }
    GuestAddr base() const { return base_; }
    u64 size() const { return size_; }

    // Initial stack pointer: one past the highest usable byte, 16-byte aligned.
    GuestAddr top() const { return base_ + size_; }

private:
    GuestStack(memory::AddressSpace& space, GuestAddr base, u64 size) : space_(&space), base_(base), size_(size) {}
    void release();

    memory::AddressSpace* space_ = nullptr;
    GuestAddr base_ = 0;
    u64 size_ = 0;
};

} // namespace zenith::cpu
