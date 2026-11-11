#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace gateway::core {

/**
 * @brief C++20 stand-in for std::expected<T, std::string>.
 *
 * The design specifies std::expected, which is a C++23 type. This wrapper
 * keeps the same success/failure shape without raising the language standard.
 */
template <typename T>
class Expected {
public:
    static Expected success(T value) {
        Expected result;
        result.value_ = std::move(value);
        return result;
    }

    static Expected failure(std::string error) {
        Expected result;
        result.error_ = std::move(error);
        return result;
    }

    [[nodiscard]] bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & { return checked(); }
    [[nodiscard]] const T& value() const& { return checked(); }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    Expected() = default;

    T& checked() {
        if (!value_) {
            throw std::logic_error("Expected has no value: " + error_);
        }
        return *value_;
    }

    const T& checked() const {
        if (!value_) {
            throw std::logic_error("Expected has no value: " + error_);
        }
        return *value_;
    }

    std::optional<T> value_;
    std::string error_;
};

}  // namespace gateway::core
