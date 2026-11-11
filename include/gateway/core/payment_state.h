#pragma once

#include "gateway/blockchain/models.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gateway::core {

struct PendingVerification {
    std::string tx_hash;
    std::string wallet_address;
    std::string endpoint_path;
    double price_usdc = 0.0;
    std::string status;
    std::uint64_t updated_at_unix = 0;

    bool operator==(const PendingVerification& other) const = default;
};

struct PersistedGatewayState {
    std::vector<gateway::models::RefundableTransaction> refunds;
    std::vector<PendingVerification> pending;
    std::vector<std::string> seen_transactions;
    std::vector<std::pair<std::string, std::uint64_t>> verified_until_unix;

    bool operator==(const PersistedGatewayState& other) const = default;
};

[[nodiscard]] std::string serializeGatewayState(const PersistedGatewayState& state);
[[nodiscard]] PersistedGatewayState parseGatewayState(std::string_view json_text);

}  // namespace gateway::core
