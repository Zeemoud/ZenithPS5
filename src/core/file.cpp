// SPDX-License-Identifier: GPL-2.0-only
#include "core/file.hpp"

#include <fstream>

namespace zenith::core {
namespace {

constexpr u64 maxFileSize = 1 * GiB;

} // namespace

Result<std::vector<u8>> readFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return makeError("cannot read '{}': not a regular file", path.string());
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return makeError("cannot read '{}': cannot open the file", path.string());
    }

    const std::streamsize size = stream.tellg();
    if (size < 0) {
        return makeError("cannot read '{}': cannot get the file size", path.string());
    }
    if (static_cast<u64>(size) > maxFileSize) {
        return makeError("cannot read '{}': file too large ({} bytes)", path.string(), size);
    }

    std::vector<u8> bytes(static_cast<usize>(size));
    stream.seekg(0);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): istream::read takes char*
    if (size > 0 && !stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return makeError("cannot read '{}': read error", path.string());
    }
    return bytes;
}

} // namespace zenith::core
