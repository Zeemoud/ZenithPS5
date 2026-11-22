// SPDX-License-Identifier: GPL-2.0-only
#include "cpu/stub_table.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace zenith::cpu {

Result<StubTable> StubTable::create(memory::AddressSpace& space, GuestAddr base, u64 capacity, u64 slotSize) {
    if (capacity == 0) {
        return makeError("stub table: capacity must not be zero");
    }
    if (slotSize < stubSize || slotSize % 16 != 0) {
        return makeError("stub table: the slot size must be a multiple of 16, at least {}", stubSize);
    }
    const u64 bytes = capacity * slotSize;
    const u64 regionSize = (bytes + memory::guestPageSize - 1) / memory::guestPageSize * memory::guestPageSize;

    if (!space.map(base, regionSize, platform::readExecute, "hle stubs")) {
        return makeError("stub table: cannot map {:#x} bytes at {:#x}", regionSize, base);
    }
    return StubTable(space, base, capacity, slotSize, regionSize);
}

StubTable::~StubTable() {
    release();
}

StubTable::StubTable(StubTable&& other) noexcept
    : space_(std::exchange(other.space_, nullptr)), base_(std::exchange(other.base_, 0)),
      capacity_(std::exchange(other.capacity_, 0)), slotSize_(std::exchange(other.slotSize_, stubSize)),
      regionSize_(std::exchange(other.regionSize_, 0)), count_(std::exchange(other.count_, 0)) {}

StubTable& StubTable::operator=(StubTable&& other) noexcept {
    if (this != &other) {
        release();
        space_ = std::exchange(other.space_, nullptr);
        base_ = std::exchange(other.base_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
        slotSize_ = std::exchange(other.slotSize_, stubSize);
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

Result<GuestAddr> StubTable::emit(const u8* code, usize length) {
    if (count_ >= capacity_) {
        return makeError("stub table is full ({} stubs)", capacity_);
    }

    const GuestAddr address = base_ + count_ * slotSize_;
    if (!space_->protect(base_, regionSize_, platform::readWrite)) {
        return makeError("stub table: cannot make the table writable");
    }

    u8* slot = memory::AddressSpace::hostPointer(address);
    std::memcpy(slot, code, length);
    std::memset(slot + length, 0xCC, slotSize_ - length); // int3 padding

    if (!space_->protect(base_, regionSize_, platform::readExecute)) {
        return makeError("stub table: cannot make the table executable again");
    }
    ++count_;
    return address;
}

Result<GuestAddr> StubTable::add(HostFunction function) {
    const u64 target = static_cast<u64>(reinterpret_cast<std::uintptr_t>(function));

    u8 code[13];
    code[0] = 0x49; // movabs r11, imm64
    code[1] = 0xBB;
    std::memcpy(code + 2, &target, sizeof(target));
    code[10] = 0x41; // jmp r11
    code[11] = 0xFF;
    code[12] = 0xE3;
    return emit(code, sizeof(code));
}

Result<GuestAddr> StubTable::addWithId(HostFunction function, u32 id) {
    if (slotSize_ < idStubSize) {
        return makeError("stub table: slots of {} bytes are too small for a stub with an id (need {})", slotSize_,
                         idStubSize);
    }
    const u64 target = static_cast<u64>(reinterpret_cast<std::uintptr_t>(function));

    u8 code[18];
    code[0] = 0xBF; // mov edi, imm32
    std::memcpy(code + 1, &id, sizeof(id));
    code[5] = 0x49; // movabs r11, imm64
    code[6] = 0xBB;
    std::memcpy(code + 7, &target, sizeof(target));
    code[15] = 0x41; // jmp r11
    code[16] = 0xFF;
    code[17] = 0xE3;
    return emit(code, sizeof(code));
}

} // namespace zenith::cpu
