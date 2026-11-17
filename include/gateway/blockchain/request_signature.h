#pragma once

#include "gateway/core/Configuration.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gateway::blockchain {

struct AgentRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string wallet;
    std::string quote_reference;
    std::uint64_t expiry_unix = 0;
    std::uint64_t chain_id = 0;
    std::string verifying_contract;
};

[[nodiscard]] std::uint64_t chainIdOf(gateway::core::Network network);

[[nodiscard]] std::array<std::uint8_t, 32> agentRequestDigest(const AgentRequest& request);

[[nodiscard]] std::optional<std::string> addressFromPrivateKey(std::string_view private_key_hex);

[[nodiscard]] std::optional<std::string> recoverSigner(const std::array<std::uint8_t, 32>& digest,
                                                      std::string_view signature_hex);

[[nodiscard]] std::optional<std::array<std::uint8_t, 65>> signDigest(std::string_view private_key_hex,
                                                                      const std::array<std::uint8_t, 32>& digest);

[[nodiscard]] std::optional<std::string> signAgentRequest(std::string_view private_key_hex,
                                                         const AgentRequest& request);

}  // namespace gateway::blockchain
