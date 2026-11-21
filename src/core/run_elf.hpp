// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"
#include "core/bridge.hpp"

#include <span>
#include <string_view>

namespace zenith::core {

struct RunOptions {
    u64 stackSize = 1 * MiB;        // size of the main guest stack, guard area included
    GuestAddr imageBase = 0x400000; // where a position-independent program (ET_DYN) is loaded
    OutputSink output;              // where the guest's output goes; empty means the host's stdout/stderr
};

// Loads a plain ELF executable into a fresh guest address space, installs the test bridge, runs the entry
// point on a new guest stack, and returns the program's result: the value of RAX if the entry point returns,
// or the code given to the bridge's exit. Everything is released before returning.
//
// `name` is only used in log and error messages.
[[nodiscard]] Result<u64> runElf(std::span<const u8> data, std::string_view name, const RunOptions& options = {});

} // namespace zenith::core
