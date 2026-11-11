#pragma once

#include <string_view>

namespace gateway::core {

[[nodiscard]] inline bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] inline bool isValidHttpUrl(std::string_view url) {
    if (startsWith(url, "https://")) {
        return url.size() > std::string_view("https://").size();
    }
    if (startsWith(url, "http://")) {
        return url.size() > std::string_view("http://").size();
    }
    return false;
}

[[nodiscard]] inline bool isValidRpcUrl(std::string_view url) {
    if (isValidHttpUrl(url)) {
        return true;
    }
    if (startsWith(url, "wss://")) {
        return url.size() > std::string_view("wss://").size();
    }
    if (startsWith(url, "ws://")) {
        return url.size() > std::string_view("ws://").size();
    }
    return false;
}

[[nodiscard]] inline bool isValidLogLevel(std::string_view level) {
    return level == "trace" || level == "debug" || level == "info" || level == "warn" ||
           level == "error";
}

[[nodiscard]] inline bool isValidAddress(std::string_view address) {
    if (address.size() != 42 || address[0] != '0' || (address[1] != 'x' && address[1] != 'X')) {
        return false;
    }
    for (std::size_t i = 2; i < address.size(); ++i) {
        const char c = address[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

}  // namespace gateway::core
