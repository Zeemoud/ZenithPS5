// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"

#include <span>
#include <string_view>

namespace zenith::core {

struct RunOptions {
    u64 stackSize = 1 * MiB; // size of the main guest stack, guard area included
};

// Loads a plain ELF executable into a fresh guest address space, runs its entry point on a new guest stack,
// and returns the value the entry point returned in RAX. Everything is released before returning.
//
// `name` is only used in log and error messages.
[[nodiscard]] Result<u64> runElf(std::span<const u8> data, std::string_view name, const RunOptions& options = {});

} // namespace zenith::core
