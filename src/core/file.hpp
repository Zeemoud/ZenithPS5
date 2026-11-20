// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"

#include <filesystem>
#include <vector>

namespace zenith::core {

// Reads a whole regular file into memory.
[[nodiscard]] Result<std::vector<u8>> readFile(const std::filesystem::path& path);

} // namespace zenith::core
