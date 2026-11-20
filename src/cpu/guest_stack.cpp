// SPDX-License-Identifier: GPL-2.0-only
#include "cpu/guest_stack.hpp"

#include <format>
#include <utility>

namespace zenith::cpu {

Result<GuestStack> GuestStack::create(memory::AddressSpace& space, GuestAddr base, u64 size, std::string_view name) {
    if (size <= stackGuardSize) {
        return makeError("stack '{}': size {:#x} must be larger than the guard area", name, size);
    }

    const GuestAddr usableStart = base + stackGuardSize;
    const u64 usableSize = size - stackGuardSize;

    if (!space.map(base, stackGuardSize, platform::Protection::None, std::format("{}:guard", name))) {
        return makeError("stack '{}': cannot map the guard area at {:#x}", name, base);
    }
    if (!space.map(usableStart, usableSize, platform::readWrite, name)) {
        static_cast<void>(space.unmap(base, stackGuardSize));
        return makeError("stack '{}': cannot map {:#x} bytes at {:#x}", name, usableSize, usableStart);
    }
    return GuestStack(space, base, size);
}

GuestStack::~GuestStack() {
    release();
}

GuestStack::GuestStack(GuestStack&& other) noexcept
    : space_(std::exchange(other.space_, nullptr)), base_(std::exchange(other.base_, 0)),
      size_(std::exchange(other.size_, 0)) {}

GuestStack& GuestStack::operator=(GuestStack&& other) noexcept {
    if (this != &other) {
        release();
        space_ = std::exchange(other.space_, nullptr);
        base_ = std::exchange(other.base_, 0);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void GuestStack::release() {
    if (space_ != nullptr) {
        static_cast<void>(space_->unmap(base_ + stackGuardSize, size_ - stackGuardSize));
        static_cast<void>(space_->unmap(base_, stackGuardSize));
        space_ = nullptr;
    }
}

} // namespace zenith::cpu
