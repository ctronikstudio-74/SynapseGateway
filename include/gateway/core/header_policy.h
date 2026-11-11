#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::core {

struct HttpHeader {
    std::string name;
    std::string value;

    bool operator==(const HttpHeader& other) const = default;
};

[[nodiscard]] bool isPaymentHeader(std::string_view name);
[[nodiscard]] bool isHopByHopHeader(std::string_view name);
[[nodiscard]] bool isSafeHeaderName(std::string_view name);
[[nodiscard]] std::string sanitizeHeaderValue(std::string_view value);
[[nodiscard]] std::vector<HttpHeader> headersForBackend(const std::vector<HttpHeader>& incoming);
[[nodiscard]] std::optional<std::string> headerValue(const std::vector<HttpHeader>& headers, std::string_view name);
[[nodiscard]] bool hasAgentSignature(const std::vector<HttpHeader>& headers);

}  // namespace gateway::core
