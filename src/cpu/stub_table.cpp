// SPDX-License-Identifier: GPL-2.0-only
#include "cpu/stub_table.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace zenith::cpu {

Result<StubTable> StubTable::create(memory::AddressSpace& space, GuestAddr base, u64 capacity) {
    if (capacity == 0) {
        return makeError("stub table: capacity must not be zero");
    }
    const u64 bytes = capacity * stubSize;
    const u64 regionSize = (bytes + memory::guestPageSize - 1) / memory::guestPageSize * memory::guestPageSize;

    if (!space.map(base, regionSize, platform::readExecute, "hle stubs")) {
        return makeError("stub table: cannot map {:#x} bytes at {:#x}", regionSize, base);
    }
    return StubTable(space, base, capacity, regionSize);
}

StubTable::~StubTable() {
    release();
}

StubTable::StubTable(StubTable&& other) noexcept
    : space_(std::exchange(other.space_, nullptr)), base_(std::exchange(other.base_, 0)),
      capacity_(std::exchange(other.capacity_, 0)), regionSize_(std::exchange(other.regionSize_, 0)),
      count_(std::exchange(other.count_, 0)) {}

StubTable& StubTable::operator=(StubTable&& other) noexcept {
    if (this != &other) {
        release();
        space_ = std::exchange(other.space_, nullptr);
        base_ = std::exchange(other.base_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
        regionSize_ = std::exchange(other.regionSize_, 0);
        count_ = std::exchange(other.count_, 0);
    }
    return *this;
}

void StubTable::release() {
    if (space_ != nullptr) {
        static_cast<void>(space_->unmap(base_, regionSize_));
        space_ = nullptr;
    }
}

Result<GuestAddr> StubTable::add(HostFunction function) {
    if (count_ >= capacity_) {
        return makeError("stub table is full ({} stubs)", capacity_);
    }

    const GuestAddr address = base_ + count_ * stubSize;
    if (!space_->protect(base_, regionSize_, platform::readWrite)) {
        return makeError("stub table: cannot make the table writable");
    }

    const u64 target = static_cast<u64>(reinterpret_cast<std::uintptr_t>(function));
    u8* code = memory::AddressSpace::hostPointer(address);
    code[0] = 0x49; // movabs r11, imm64
    code[1] = 0xBB;
    std::memcpy(code + 2, &target, sizeof(target));
    code[10] = 0x41; // jmp r11
    code[11] = 0xFF;
    code[12] = 0xE3;
    code[13] = 0xCC; // int3 padding
    code[14] = 0xCC;
    code[15] = 0xCC;

    if (!space_->protect(base_, regionSize_, platform::readExecute)) {
        return makeError("stub table: cannot make the table executable again");
    }
    ++count_;
    return address;
}

} // namespace zenith::cpu
