// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "common/assert.hpp"

#include <format>
#include <string>
#include <utility>
#include <variant>

// Result<T>: either a value or an error message. For failures that are expected and recoverable
// (a malformed input file, a mapping that is refused...). Programming errors use ZENITH_VERIFY.
//
//   Result<int> parse(std::string_view text) {
//       if (text.empty()) return makeError("empty input");
//       return 42;
//   }
namespace zenith {

struct Error {
    std::string message;
};

template <class... Args> Error makeError(std::format_string<Args...> fmt, Args&&... args) {
    return Error{std::format(fmt, std::forward<Args>(args)...)};
}

template <class T> class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return data_.index() == 0; }
    explicit operator bool() const { return ok(); }

    T& value() & {
        ZENITH_VERIFY(ok(), "Result holds an error: {}", std::get<1>(data_).message);
        return std::get<0>(data_);
    }

    const T& value() const& {
        ZENITH_VERIFY(ok(), "Result holds an error: {}", std::get<1>(data_).message);
        return std::get<0>(data_);
    }

    T&& value() && {
        ZENITH_VERIFY(ok(), "Result holds an error: {}", std::get<1>(data_).message);
        return std::get<0>(std::move(data_));
    }

    const std::string& error() const {
        ZENITH_VERIFY(!ok(), "Result holds a value, not an error");
        return std::get<1>(data_).message;
    }

private:
    std::variant<T, Error> data_;
};

} // namespace zenith
