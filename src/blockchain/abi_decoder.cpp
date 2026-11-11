#include "gateway/blockchain/abi_decoder.h"

#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/core/validation_rules.h"

#include <cstring>
#include <stdexcept>

namespace gateway::blockchain {
namespace {

constexpr std::size_t kWord = 32;

std::vector<std::uint8_t> requireBytes(std::string_view hex_data) {
    return fromHex(hex_data);
}

void writeUint64(std::uint8_t word[kWord], std::uint64_t value) {
    for (int byte_index = 0; byte_index < 8; ++byte_index) {
        const int shift = (7 - byte_index) * 8;
        word[24 + byte_index] = static_cast<std::uint8_t>((value >> shift) & 0xff);
    }
}

std::uint64_t readUint64At(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + kWord > bytes.size()) {
        throw std::invalid_argument("ABI word is out of range");
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | bytes[offset + 24 + i];
    }
    return value;
}

std::string wordHex(const std::array<std::uint8_t, kWord>& word) {
    return toHex(std::span<const std::uint8_t>(word.data(), word.size()));
}

}  // namespace

std::array<std::uint8_t, 4> AbiDecoder::functionSelector(std::string_view signature) {
    const auto hash = keccak256(signature);
    std::array<std::uint8_t, 4> selector{};
    std::memcpy(selector.data(), hash.data(), selector.size());
    return selector;
}

std::array<std::uint8_t, 32> AbiDecoder::encodeAddress(std::string_view address) {
    if (!gateway::core::isValidAddress(address)) {
        throw std::invalid_argument("address must be 20 bytes");
    }
    const auto raw = fromHex(address);
    std::array<std::uint8_t, 32> word{};
    std::memcpy(word.data() + 12, raw.data(), raw.size());
    return word;
}

std::array<std::uint8_t, 32> AbiDecoder::encodeUint256(std::uint64_t value) {
    std::array<std::uint8_t, 32> word{};
    writeUint64(word.data(), value);
    return word;
}

std::array<std::uint8_t, 32> AbiDecoder::encodeBytes32(std::string_view hex_word) {
    auto raw = fromHex(hex_word);
    if (raw.size() > kWord) {
        throw std::invalid_argument("bytes32 payload is longer than 32 bytes");
    }
    std::array<std::uint8_t, 32> word{};
    std::memcpy(word.data() + (kWord - raw.size()), raw.data(), raw.size());
    return word;
}

std::string AbiDecoder::encodeCall(std::string_view signature,
                                   const std::vector<std::array<std::uint8_t, 32>>& words) {
    const auto selector = functionSelector(signature);
    std::string out = "0x" + toHex(std::span<const std::uint8_t>(selector.data(), selector.size()));
    for (const auto& word : words) {
        out += wordHex(word);
    }
    return out;
}

std::string AbiDecoder::encodeDynamicBytesCall(std::string_view signature,
                                               std::string_view address,
                                               std::string_view payload_hex) {
    const auto payload = fromHex(payload_hex);
    const auto selector = functionSelector(signature);
    std::vector<std::uint8_t> body;
    body.insert(body.end(), selector.begin(), selector.end());

    const auto address_word = encodeAddress(address);
    body.insert(body.end(), address_word.begin(), address_word.end());
    const auto offset_word = encodeUint256(64);
    body.insert(body.end(), offset_word.begin(), offset_word.end());
    const auto length_word = encodeUint256(static_cast<std::uint64_t>(payload.size()));
    body.insert(body.end(), length_word.begin(), length_word.end());

    const std::size_t padded = ((payload.size() + 31) / 32) * 32;
    body.insert(body.end(), payload.begin(), payload.end());
    body.resize(body.size() + (padded - payload.size()), 0);
    return "0x" + toHex(body);
}

std::string AbiDecoder::encodeIdentityReturn(std::string_view identity_type,
                                             std::uint64_t registration_timestamp) {
    std::vector<std::uint8_t> body;
    const auto offset = encodeUint256(64);
    const auto timestamp = encodeUint256(registration_timestamp);
    const auto length = encodeUint256(static_cast<std::uint64_t>(identity_type.size()));
    body.insert(body.end(), offset.begin(), offset.end());
    body.insert(body.end(), timestamp.begin(), timestamp.end());
    body.insert(body.end(), length.begin(), length.end());
    const std::size_t padded = ((identity_type.size() + 31) / 32) * 32;
    body.insert(body.end(),
                reinterpret_cast<const std::uint8_t*>(identity_type.data()),
                reinterpret_cast<const std::uint8_t*>(identity_type.data() + identity_type.size()));
    body.resize(body.size() + (padded - identity_type.size()), 0);
    return "0x" + toHex(body);
}

std::string AbiDecoder::encodeReputationReturn(std::uint64_t success_rate_bps,
                                               std::uint64_t avg_response_time_ms,
                                               std::uint64_t feedback_score_bps,
                                               std::uint64_t total_requests,
                                               std::uint64_t last_updated) {
    std::string out = "0x";
    for (const std::uint64_t value :
         {success_rate_bps, avg_response_time_ms, feedback_score_bps, total_requests, last_updated}) {
        out += wordHex(encodeUint256(value));
    }
    return out;
}

std::uint64_t AbiDecoder::decodeUint64(std::string_view hex_data, std::size_t word_index) {
    const auto bytes = requireBytes(hex_data);
    return readUint64At(bytes, word_index * kWord);
}

std::string AbiDecoder::decodeAddress(std::string_view hex_data, std::size_t word_index) {
    const auto bytes = requireBytes(hex_data);
    const std::size_t offset = word_index * kWord;
    if (offset + kWord > bytes.size()) {
        throw std::invalid_argument("ABI address word is out of range");
    }
    return "0x" + toHex(std::span<const std::uint8_t>(bytes.data() + offset + 12, 20));
}

bool AbiDecoder::decodeBool(std::string_view hex_data, std::size_t word_index) {
    return decodeUint64(hex_data, word_index) != 0;
}

std::string AbiDecoder::decodeString(std::string_view hex_data, std::size_t word_index) {
    const auto bytes = requireBytes(hex_data);
    const std::uint64_t offset = readUint64At(bytes, word_index * kWord);
    if (offset > bytes.size()) {
        throw std::invalid_argument("ABI string offset is out of range");
    }
    const std::uint64_t length = readUint64At(bytes, static_cast<std::size_t>(offset));
    const std::size_t start = static_cast<std::size_t>(offset) + kWord;
    if (length > bytes.size() - start) {
        throw std::invalid_argument("ABI string length is out of range");
    }
    return std::string(reinterpret_cast<const char*>(bytes.data() + start), static_cast<std::size_t>(length));
}

IdentityNFT AbiDecoder::decodeIdentityData(std::string_view hex_data,
                                          std::string token_id,
                                          std::string owner_address) {
    IdentityNFT nft;
    nft.token_id = std::move(token_id);
    nft.owner_address = std::move(owner_address);
    nft.identity_type = decodeString(hex_data, 0);
    nft.registration_timestamp = decodeUint64(hex_data, 1);
    return nft;
}

ReputationData AbiDecoder::decodeReputation(std::string_view hex_data, std::string wallet_address) {
    ReputationData data;
    data.wallet_address = std::move(wallet_address);
    const auto clamp_unit = [](std::uint64_t basis_points) {
        const double normalized = static_cast<double>(basis_points) / 10000.0;
        if (normalized < 0.0) {
            return 0.0;
        }
        if (normalized > 1.0) {
            return 1.0;
        }
        return normalized;
    };
    data.success_rate = clamp_unit(decodeUint64(hex_data, 0));
    data.avg_response_time_ms = static_cast<double>(decodeUint64(hex_data, 1));
    data.feedback_score = clamp_unit(decodeUint64(hex_data, 2));
    data.total_requests = decodeUint64(hex_data, 3);
    data.last_updated = decodeUint64(hex_data, 4);
    return data;
}

}  // namespace gateway::blockchain
