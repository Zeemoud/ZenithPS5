// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"
#include "core/file.hpp"
#include "core/run_elf.hpp"

#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr char usageText[] = R"(Usage: zenith [options] <program.elf>

Loads a plain x86-64 ELF executable (ET_EXEC) and runs it.

Options:
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

    const std::span<char*> args(argv, static_cast<usize>(argc));
    constexpr std::string_view logLevelPrefix = "--log-level=";
    std::string path;

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

    const auto result = core::runElf(bytes.value(), path);
    if (!result.ok()) {
        std::fputs(std::format("error: {}\n", result.error()).c_str(), stderr);
        return 1;
    }

    std::fputs(std::format("guest returned {} ({:#x})\n", result.value(), result.value()).c_str(), stdout);
    return 0;
}
