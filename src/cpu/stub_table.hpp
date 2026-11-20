// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "memory/address_space.hpp"

namespace zenith::cpu {

// A host function callable from guest code. It receives the six integer argument registers and
// returns the value placed in RAX. It runs natively, on the guest stack, with guest-ABI alignment.
// It must not throw: an exception cannot cross guest frames (hence noexcept).
using HostFunction = u64 (*)(u64, u64, u64, u64, u64, u64) noexcept;

// A table of small code stubs in guest memory. Each stub, when called by guest code, jumps to a host
// function. This is the bridge used by the HLE: imports of guest code are resolved to stub addresses.
//
//   stub:  movabs r11, <host function>   ; r11 is a scratch register in the System V ABI
//          jmp    r11
//
// Add all the stubs before running guest code: adding one briefly makes the table writable.
// The mappings are released when the object is destroyed, so it must not outlive its AddressSpace.
class StubTable {
public:
    static constexpr u64 stubSize = 16;

    [[nodiscard]] static Result<StubTable> create(memory::AddressSpace& space, GuestAddr base, u64 capacity);

    ~StubTable();

    StubTable(const StubTable&) = delete;
    StubTable& operator=(const StubTable&) = delete;
    StubTable(StubTable&& other) noexcept;
    StubTable& operator=(StubTable&& other) noexcept;

    // Creates a stub for `function` and returns its guest address.
    [[nodiscard]] Result<GuestAddr> add(HostFunction function);

    u64 size() const { return count_; }
    u64 capacity() const { return capacity_; }

private:
    StubTable(memory::AddressSpace& space, GuestAddr base, u64 capacity, u64 regionSize)
        : space_(&space), base_(base), capacity_(capacity), regionSize_(regionSize) {}
    void release();

    memory::AddressSpace* space_ = nullptr;
    GuestAddr base_ = 0;
    u64 capacity_ = 0;
    u64 regionSize_ = 0;
    u64 count_ = 0;
};

} // namespace zenith::cpu
