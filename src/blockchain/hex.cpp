#include "gateway/blockchain/hex.h"

#include <cctype>
#include <stdexcept>

namespace gateway::blockchain {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    throw std::invalid_argument("invalid hex digit");
}

std::string_view stripPrefix(std::string_view hex) {
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex.remove_prefix(2);
    }
    return hex;
}

}  // namespace

std::string toHex(std::span<const std::uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = kDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kDigits[bytes[i] & 0x0f];
    }
    return out;
}

std::vector<std::uint8_t> fromHex(std::string_view hex) {
    hex = stripPrefix(hex);
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("hex string must have even length");
    }
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexValue(hex[i]);
        const int lo = hexValue(hex[i + 1]);
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string hexToDecimal(std::string_view hex) {
    hex = stripPrefix(hex);
    if (hex.empty()) {
        return "0";
    }
    std::vector<std::uint8_t> digits{0};
    for (const char c : hex) {
        int carry = hexValue(c);
        for (std::uint8_t& digit : digits) {
            const int current = static_cast<int>(digit) * 16 + carry;
            digit = static_cast<std::uint8_t>(current % 10);
            carry = current / 10;
        }
        while (carry > 0) {
            digits.push_back(static_cast<std::uint8_t>(carry % 10));
            carry /= 10;
        }
    }
    while (digits.size() > 1 && digits.back() == 0) {
        digits.pop_back();
    }
    std::string out;
    out.reserve(digits.size());
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        out.push_back(static_cast<char>('0' + *it));
    }
    return out;
}

std::uint64_t hexToUint64(std::string_view hex) {
    hex = stripPrefix(hex);
    if (hex.empty()) {
        return 0;
    }
    if (hex.size() > 16) {
        hex.remove_prefix(hex.size() - 16);
    }
    std::uint64_t value = 0;
    for (const char c : hex) {
        value = (value << 4) | static_cast<std::uint64_t>(hexValue(c));
    }
    return value;
}

std::string normalizeAddress(std::string_view address) {
    std::string out(address);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace gateway::blockchain
