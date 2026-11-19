// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <format>
#include <string>
#include <utility>

// Assertions. The optional message is a std::format string plus arguments.
//
//   ZENITH_VERIFY(ptr != nullptr, "null pointer for handle {}", handle);  // always checked
//   ZENITH_ASSERT(size % 16 == 0);                                        // debug builds only
//   ZENITH_UNREACHABLE("unknown opcode {:#x}", opcode);                   // always fatal
//   ZENITH_UNIMPLEMENTED("sceKernelFoo");                                 // always fatal
//
// A failure logs a Fatal message, then aborts.
namespace zenith {

[[noreturn]] void assertFail(const char* kind, const char* expression, const char* file, int line, const char* function,
                             const std::string& message);

namespace detail {

inline std::string formatMessage() {
    return {};
}

template <class... Args> std::string formatMessage(std::format_string<Args...> fmt, Args&&... args) {
    return std::format(fmt, std::forward<Args>(args)...);
}

} // namespace detail
} // namespace zenith

#define ZENITH_DETAIL_FAIL(kind, expression, ...)                                                                      \
    ::zenith::assertFail(kind, expression, __FILE__, __LINE__, __func__, ::zenith::detail::formatMessage(__VA_ARGS__))

#define ZENITH_DETAIL_CHECK(kind, cond, ...)                                                                           \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            ZENITH_DETAIL_FAIL(kind, #cond __VA_OPT__(, ) __VA_ARGS__);                                                \
        }                                                                                                              \
    } while (false)

#define ZENITH_VERIFY(cond, ...) ZENITH_DETAIL_CHECK("VERIFY", cond __VA_OPT__(, ) __VA_ARGS__)

#ifdef NDEBUG
#define ZENITH_ASSERT(cond, ...) static_cast<void>(sizeof(!(cond)))
#else
#define ZENITH_ASSERT(cond, ...) ZENITH_DETAIL_CHECK("ASSERT", cond __VA_OPT__(, ) __VA_ARGS__)
#endif

#define ZENITH_UNREACHABLE(...)                                                                                        \
    do {                                                                                                               \
        ZENITH_DETAIL_FAIL("UNREACHABLE", "" __VA_OPT__(, ) __VA_ARGS__);                                              \
    } while (false)

#define ZENITH_UNIMPLEMENTED(...)                                                                                      \
    do {                                                                                                               \
        ZENITH_DETAIL_FAIL("UNIMPLEMENTED", "" __VA_OPT__(, ) __VA_ARGS__);                                            \
    } while (false)
