// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/file.hpp"
#include "core/library_source.hpp"
#include "core/run_elf.hpp"
#include "cpu/crash_handler.hpp"

#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr char usageText[] = R"(Usage: zenith [options] <program.elf>

Loads an x86-64 ELF program (fixed-address or position-independent) and runs it. The shared libraries it needs
are provided by the host, or looked up as files in the library directories.

Options:
  -L, --library-path <dir>  look for shared libraries in <dir> (repeatable); the program's own directory
                            is always searched last
  -l, --log-level <level>   trace, debug, info (default), warn, error or off
  -h, --help                show this help
)";

std::optional<zenith::log::Level> parseLevel(std::string_view text) {
    using zenith::log::Level;
    if (text == "trace")
        return Level::Trace;
    if (text == "debug")
        return Level::Debug;
    if (text == "info")
        return Level::Info;
    if (text == "warn")
        return Level::Warn;
    if (text == "error")
        return Level::Error;
    if (text == "off")
        return Level::Off;
    return std::nullopt;
}

int usageError(std::string_view message) {
    std::fputs(std::format("error: {}\n\n{}", message, usageText).c_str(), stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    using namespace zenith;

    cpu::installCrashHandler();

    const std::span<char*> args(argv, static_cast<usize>(argc));
    constexpr std::string_view logLevelPrefix = "--log-level=";
    constexpr std::string_view libraryPathPrefix = "--library-path=";
    std::string path;
    std::vector<std::filesystem::path> libraryDirectories;

    for (usize i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help") {
            std::fputs(usageText, stdout);
            return 0;
        }

        if (arg == "-l" || arg == "--log-level" || arg.starts_with(logLevelPrefix)) {
            std::string_view value;
            if (arg.starts_with(logLevelPrefix)) {
                value = arg.substr(logLevelPrefix.size());
            } else if (i + 1 < args.size()) {
                value = args[++i];
            } else {
                return usageError(std::format("option '{}' needs a value", arg));
            }
            const auto level = parseLevel(value);
            if (!level.has_value()) {
                return usageError(std::format("unknown log level '{}'", value));
            }
            log::setLevel(*level);
            continue;
        }

        if (arg == "-L" || arg == "--library-path" || arg.starts_with(libraryPathPrefix)) {
            if (arg.starts_with(libraryPathPrefix)) {
                libraryDirectories.emplace_back(arg.substr(libraryPathPrefix.size()));
            } else if (i + 1 < args.size()) {
                libraryDirectories.emplace_back(args[++i]);
            } else {
                return usageError(std::format("option '{}' needs a value", arg));
            }
            continue;
        }

        if (arg.starts_with('-')) {
            return usageError(std::format("unknown option '{}'", arg));
        }
        if (!path.empty()) {
            return usageError("too many arguments");
        }
        path = arg;
    }

    if (path.empty()) {
        return usageError("no program given");
    }

    const auto bytes = core::readFile(path);
    if (!bytes.ok()) {
        std::fputs(std::format("error: {}\n", bytes.error()).c_str(), stderr);
        return 1;
    }

    // The program's own directory comes last, so a library given with -L takes precedence.
    const std::filesystem::path programDirectory = std::filesystem::path(path).parent_path();
    libraryDirectories.push_back(programDirectory.empty() ? std::filesystem::path(".") : programDirectory);

    core::RunOptions options;
    options.libraries = core::directoryLibrarySource(std::move(libraryDirectories));
    const auto result = core::runElf(bytes.value(), path, options);
    if (!result.ok()) {
        std::fputs(std::format("error: {}\n", result.error()).c_str(), stderr);
        return 1;
    }

    std::fputs(std::format("guest returned {} ({:#x})\n", result.value(), result.value()).c_str(), stdout);
    return 0;
}
