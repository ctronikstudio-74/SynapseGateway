#include "gateway/core/header_policy.h"

#include <cctype>

namespace gateway::core {
namespace {

std::string lowerCopy(std::string_view value) {
    std::string lowered(value);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool isPaymentHeader(std::string_view name) {
    const std::string lowered = lowerCopy(name);
    if (lowered.rfind("x-payment", 0) == 0 || lowered.rfind("x-402", 0) == 0) {
        return true;
    }
    return lowered == "x-transaction-hash" || lowered == "x-agent-signature";
}

bool isHopByHopHeader(std::string_view name) {
    const std::string lowered = lowerCopy(name);
    return lowered == "connection" || lowered == "keep-alive" || lowered == "proxy-authenticate" ||
           lowered == "proxy-authorization" || lowered == "te" || lowered == "trailer" ||
           lowered == "transfer-encoding" || lowered == "upgrade" || lowered == "host" ||
           lowered == "content-length";
}

bool isSafeHeaderName(std::string_view name) {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    for (const unsigned char character : name) {
        const bool token = std::isalnum(character) != 0 || character == '!' || character == '#' || character == '$' ||
                           character == '%' || character == '&' || character == '\'' || character == '*' ||
                           character == '+' || character == '-' || character == '.' || character == '^' ||
                           character == '_' || character == '`' || character == '|' || character == '~';
        if (!token) {
            return false;
        }
    }
    return true;
}

std::string sanitizeHeaderValue(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char character : value) {
        if (character >= 0x20 && character != 0x7f) {
            sanitized.push_back(static_cast<char>(character));
        }
    }
    return sanitized;
}

std::vector<HttpHeader> headersForBackend(const std::vector<HttpHeader>& incoming) {
    std::vector<HttpHeader> forwarded;
    forwarded.reserve(incoming.size());
    for (const HttpHeader& header : incoming) {
        if (!isSafeHeaderName(header.name) || isPaymentHeader(header.name) || isHopByHopHeader(header.name)) {
            continue;
        }
        forwarded.push_back(HttpHeader{header.name, sanitizeHeaderValue(header.value)});
    }
    return forwarded;
}

std::optional<std::string> headerValue(const std::vector<HttpHeader>& headers, std::string_view name) {
    for (const HttpHeader& header : headers) {
        if (equalsIgnoreCase(header.name, name)) {
            return header.value;
        }
    }
    return std::nullopt;
}

bool hasAgentSignature(const std::vector<HttpHeader>& headers) {
    const auto signature = headerValue(headers, "X-Agent-Signature");
    if (!signature || signature->size() < 64 || signature->size() > 512) {
        return false;
    }
    for (const unsigned char character : *signature) {
        if (character < 0x20 || character == 0x7f) {
            return false;
        }
        const bool allowed = std::isalnum(character) != 0 || character == '+' || character == '/' || character == '=' ||
                             character == '-' || character == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

}  // namespace gateway::core
