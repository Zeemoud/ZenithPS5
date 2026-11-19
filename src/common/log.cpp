// SPDX-License-Identifier: GPL-2.0-only
#include "common/log.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace zenith::log {
namespace {

struct State {
    std::array<std::atomic<Level>, channelCount> levels;
    std::mutex mutex;
    Sink sink;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    State() {
        for (auto& level : levels) {
            level.store(Level::Info);
        }
    }
};

State& state() {
    static State instance;
    return instance;
}

char levelChar(Level level) {
    switch (level) {
    case Level::Trace:
        return 'T';
    case Level::Debug:
        return 'D';
    case Level::Info:
        return 'I';
    case Level::Warn:
        return 'W';
    case Level::Error:
        return 'E';
    case Level::Fatal:
        return 'F';
    case Level::Off:
        break;
    }
    return '-';
}

u32 threadIndex() {
    static std::atomic<u32> next{0};
    thread_local const u32 index = next.fetch_add(1);
    return index;
}

} // namespace

std::string_view toString(Channel channel) {
    static constexpr std::array<std::string_view, channelCount> names = {
        "Common", "Core", "Loader", "Memory", "Cpu", "Kernel", "Libs", "Gpu", "Audio", "Input", "Debug", "Frontend",
    };
    const auto index = static_cast<usize>(channel);
    return index < names.size() ? names[index] : "?";
}

void setLevel(Level level) {
    for (auto& entry : state().levels) {
        entry.store(level);
    }
}

void setLevel(Channel channel, Level level) {
    const auto index = static_cast<usize>(channel);
    if (index < channelCount) {
        state().levels[index].store(level);
    }
}

Level getLevel(Channel channel) {
    const auto index = static_cast<usize>(channel);
    return index < channelCount ? state().levels[index].load() : Level::Off;
}

bool isEnabled(Channel channel, Level level) {
    return level != Level::Off && level >= getLevel(channel);
}

void setSink(Sink sink) {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    s.sink = std::move(sink);
}

void write(Level level, Channel channel, std::string_view message) {
    State& s = state();
    const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - s.start).count();
    const std::string line = std::format("[{:9.3f}] [{}] [{}] [T{}] {}", seconds, levelChar(level), toString(channel),
                                         threadIndex(), message);

    const std::lock_guard lock(s.mutex);
    if (s.sink) {
        s.sink(level, channel, line);
        return;
    }
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
}

void flush() {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    std::fflush(stderr);
}

} // namespace zenith::log
