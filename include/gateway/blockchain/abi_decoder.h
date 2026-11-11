#pragma once

#include "gateway/blockchain/models.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::blockchain {

class AbiDecoder {
public:
    [[nodiscard]] static std::array<std::uint8_t, 4> functionSelector(std::string_view signature);
    [[nodiscard]] static std::array<std::uint8_t, 32> encodeAddress(std::string_view address);
    [[nodiscard]] static std::array<std::uint8_t, 32> encodeUint256(std::uint64_t value);
    [[nodiscard]] static std::array<std::uint8_t, 32> encodeBytes32(std::string_view hex_word);

    [[nodiscard]] static std::string encodeCall(std::string_view signature,
                                                const std::vector<std::array<std::uint8_t, 32>>& words);

    [[nodiscard]] static std::string encodeDynamicBytesCall(std::string_view signature,
                                                            std::string_view address,
                                                            std::string_view payload_hex);

    [[nodiscard]] static std::string encodeIdentityReturn(std::string_view identity_type,
                                                          std::uint64_t registration_timestamp);
    [[nodiscard]] static std::string encodeReputationReturn(std::uint64_t success_rate_bps,
                                                            std::uint64_t avg_response_time_ms,
                                                            std::uint64_t feedback_score_bps,
                                                            std::uint64_t total_requests,
                                                            std::uint64_t last_updated);

    [[nodiscard]] static std::uint64_t decodeUint64(std::string_view hex_data, std::size_t word_index);
    [[nodiscard]] static std::string decodeAddress(std::string_view hex_data, std::size_t word_index);
    [[nodiscard]] static bool decodeBool(std::string_view hex_data, std::size_t word_index);
    [[nodiscard]] static std::string decodeString(std::string_view hex_data, std::size_t word_index);

    [[nodiscard]] static IdentityNFT decodeIdentityData(std::string_view hex_data,
                                                        std::string token_id,
                                                        std::string owner_address);
    [[nodiscard]] static ReputationData decodeReputation(std::string_view hex_data,
                                                         std::string wallet_address);
};

}  // namespace gateway::blockchain
