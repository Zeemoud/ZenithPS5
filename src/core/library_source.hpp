// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/result.hpp"
#include "common/types.hpp"

#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace zenith::core {

// Finds the file of a shared library by name (a DT_NEEDED entry such as "libmath.so") and returns its bytes.
using LibrarySource = std::function<Result<std::vector<u8>>(std::string_view name)>;

// A source that looks for the library, in order, in each of the directories.
//
// The name comes from the program, which is untrusted: only a plain file name is accepted (no directory
// separators, not "." or ".."), so a program cannot make the host read files outside the directories.
[[nodiscard]] LibrarySource directoryLibrarySource(std::vector<std::filesystem::path> directories);

} // namespace zenith::core
