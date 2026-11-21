// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "cpu/guest_stack.hpp"

#include <array>

namespace zenith::cpu {

// Integer/pointer arguments of a guest call, passed in RDI, RSI, RDX, RCX, R8, R9 (System V x86-64 ABI,
// which the PS5 uses).
using GuestArgs = std::array<u64, 6>;

// Calls the guest function at `entry`, on `stack`, as if from host code, and returns its RAX.
//
// The guest code runs natively on the calling thread. It must follow the System V ABI (preserve the
// callee-saved registers, return with `ret`). A fault in guest code is not recovered: it takes the
// process down. Fails (without running anything) if `entry` is not in an executable region.
[[nodiscard]] Result<u64> callGuest(GuestAddr entry, const GuestStack& stack, const GuestArgs& args = {});

// Ends the guest call in progress: callGuest() returns `value`, as if the guest function had returned it.
//
// Only valid from a host function that guest code called (see StubTable). The host frames between that
// function and callGuest() are abandoned: their destructors do not run, so call this when nothing in
// those frames needs cleanup. Calling it outside of a guest call is a fatal error.
[[noreturn]] void exitGuest(u64 value);

} // namespace zenith::cpu
