// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/types.hpp"

#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

// Logging.
//
//   ZENITH_LOG_INFO(Kernel, "mapped {} segments", count);
//
// Messages are filtered per channel. Arguments are not evaluated when the
// message is filtered out. All functions are thread-safe.
namespace zenith::log {

enum class Level : u8 { Trace, Debug, Info, Warn, Error, Fatal, Off };

enum class Channel : u8 {
    Common,
    Core,
    Loader,
    Memory,
    Cpu,
    Kernel,
    Libs,
    Gpu,
    Audio,
    Input,
    Debug,
    Frontend,
    Count // keep last
};

inline constexpr usize channelCount = static_cast<usize>(Channel::Count);

std::string_view toString(Channel channel);

// Set the minimum level for every channel / for one channel.
void setLevel(Level level);
void setLevel(Channel channel, Level level);
Level getLevel(Channel channel);
bool isEnabled(Channel channel, Level level);

// A sink receives each fully formatted line (without trailing newline).
// An empty sink restores the default one (stderr).
// A sink is called while the logger lock is held: it must not log.
using Sink = std::function<void(Level, Channel, std::string_view)>;
void setSink(Sink sink);

// Writes unconditionally (no level filtering).
void write(Level level, Channel channel, std::string_view message);
void flush();

template <class... Args> void print(Level level, Channel channel, std::format_string<Args...> fmt, Args&&... args) {
    write(level, channel, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace zenith::log

#define ZENITH_LOG(level, channel, ...)                                                                                \
    do {                                                                                                               \
        if (::zenith::log::isEnabled(::zenith::log::Channel::channel, ::zenith::log::Level::level)) {                  \
            ::zenith::log::print(::zenith::log::Level::level, ::zenith::log::Channel::channel, __VA_ARGS__);           \
        }                                                                                                              \
    } while (false)

#define ZENITH_LOG_TRACE(channel, ...) ZENITH_LOG(Trace, channel, __VA_ARGS__)
#define ZENITH_LOG_DEBUG(channel, ...) ZENITH_LOG(Debug, channel, __VA_ARGS__)
#define ZENITH_LOG_INFO(channel, ...) ZENITH_LOG(Info, channel, __VA_ARGS__)
#define ZENITH_LOG_WARN(channel, ...) ZENITH_LOG(Warn, channel, __VA_ARGS__)
#define ZENITH_LOG_ERROR(channel, ...) ZENITH_LOG(Error, channel, __VA_ARGS__)
#define ZENITH_LOG_FATAL(channel, ...) ZENITH_LOG(Fatal, channel, __VA_ARGS__)
