// SPDX-License-Identifier: GPL-2.0-only
#include "core/library_source.hpp"

#include "core/file.hpp"

#include <string>
#include <utility>

namespace zenith::core {
namespace {

bool isPlainFileName(std::string_view name) {
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

} // namespace

LibrarySource directoryLibrarySource(std::vector<std::filesystem::path> directories) {
    return [directories = std::move(directories)](std::string_view name) -> Result<std::vector<u8>> {
        if (!isPlainFileName(name)) {
            return makeError("'{}' is not a plain library file name", name);
        }

        for (const std::filesystem::path& directory : directories) {
            const std::filesystem::path path = directory / std::string(name);
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) {
                return readFile(path);
            }
        }

        std::string searched;
        for (const std::filesystem::path& directory : directories) {
            searched += (searched.empty() ? "" : ", ") + directory.string();
        }
        return makeError("cannot find the library '{}' (searched: {})", name, searched.empty() ? "nowhere" : searched);
    };
}

} // namespace zenith::core
