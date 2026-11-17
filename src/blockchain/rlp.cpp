#include "gateway/blockchain/rlp.h"

namespace gateway::blockchain {
namespace {

void appendLength(std::vector<std::uint8_t>& out, std::uint8_t offset, std::size_t length) {
    if (length < 56) {
        out.push_back(static_cast<std::uint8_t>(offset + length));
        return;
    }
    std::uint8_t coded[8] = {};
    int width = 0;
    auto remaining = length;
    while (remaining > 0) {
        coded[width++] = static_cast<std::uint8_t>(remaining & 0xffu);
        remaining >>= 8;
    }
    out.push_back(static_cast<std::uint8_t>(offset + 55 + width));
    for (int index = width - 1; index >= 0; --index) {
        out.push_back(coded[index]);
    }
}

}  // namespace

std::vector<std::uint8_t> rlpBytes(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> out;
    if (bytes.size() == 1 && bytes[0] < 0x80) {
        out.push_back(bytes[0]);
        return out;
    }
    appendLength(out, 0x80, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

std::vector<std::uint8_t> rlpUint(std::uint64_t value) {
    if (value == 0) {
        return rlpBytes({});
    }
    std::uint8_t coded[8] = {};
    int width = 0;
    while (value > 0) {
        coded[width++] = static_cast<std::uint8_t>(value & 0xffu);
        value >>= 8;
    }
    std::vector<std::uint8_t> big(static_cast<std::size_t>(width));
    for (int index = 0; index < width; ++index) {
        big[static_cast<std::size_t>(index)] = coded[width - 1 - index];
    }
    return rlpBytes(big);
}

std::vector<std::uint8_t> rlpList(const std::vector<std::vector<std::uint8_t>>& items) {
    std::vector<std::uint8_t> payload;
    for (const auto& item : items) {
        payload.insert(payload.end(), item.begin(), item.end());
    }
    std::vector<std::uint8_t> out;
    appendLength(out, 0xc0, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace gateway::blockchain
