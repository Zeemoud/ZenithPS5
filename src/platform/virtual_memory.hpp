// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"

// Thin, OS-independent wrapper over the host virtual memory API.
// The implementation lives in platform/<os>/virtual_memory.cpp.
//
// Typical lifecycle of a range:
//   reserveFixed -> protect (make accessible) -> decommit (give memory back) -> release
//
// Pages that become accessible for the first time (or again after decommit) are zero-filled.
namespace zenith::platform {

enum class Protection : u8 { None = 0, Read = 1, Write = 2, Execute = 4 };

constexpr Protection operator|(Protection a, Protection b) {
    return static_cast<Protection>(static_cast<u8>(a) | static_cast<u8>(b));
}

constexpr bool hasFlag(Protection value, Protection flag) {
    return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
}

inline constexpr Protection readWrite = Protection::Read | Protection::Write;
inline constexpr Protection readExecute = Protection::Read | Protection::Execute;
inline constexpr Protection readWriteExecute = Protection::Read | Protection::Write | Protection::Execute;

// Host page size in bytes.
usize pageSize();

// Reserves [address, address + size) as inaccessible memory, at exactly this address.
// Never replaces an existing mapping: returns false if any part of the range is already in use.
bool reserveFixed(void* address, usize size);

// Changes the protection of pages inside a reserved range.
bool protect(void* address, usize size, Protection protection);

// Releases the physical memory behind a range and makes it inaccessible again (the range stays reserved).
bool decommit(void* address, usize size);

// Gives the range back to the host.
bool release(void* address, usize size);

}  // namespace zenith::platform
