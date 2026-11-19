// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace zenith {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

static_assert(sizeof(f32) == 4, "f32 must be 32 bits");
static_assert(sizeof(f64) == 8, "f64 must be 64 bits");
static_assert(sizeof(void*) == 8, "ZenithPS5 requires a 64-bit host");

// Address in the guest (PS5) virtual address space.
// Always 64-bit, whatever the host pointer size is.
using GuestAddr = u64;

inline constexpr u64 KiB = 1024ULL;
inline constexpr u64 MiB = 1024ULL * KiB;
inline constexpr u64 GiB = 1024ULL * MiB;

} // namespace zenith
