// SPDX-License-Identifier: GPL-2.0-only
#include "common/assert.hpp"

#include "common/log.hpp"

#include <cstdlib>

namespace zenith {

void assertFail(const char* kind, const char* expression, const char* file, int line, const char* function,
                const std::string& message) {
    std::string text = kind;
    if (expression != nullptr && expression[0] != '\0') {
        text += std::format(" ({})", expression);
    }
    if (!message.empty()) {
        text += ": " + message;
    }
    text += std::format(" at {}:{} in {}", file, line, function);

    log::write(log::Level::Fatal, log::Channel::Common, text);
    log::flush();
    std::abort();
}

} // namespace zenith
