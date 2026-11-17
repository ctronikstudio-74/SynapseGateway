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

struct IssuedQuote {
    std::string reference;
    std::string endpoint_path;
    std::string wallet_address;
    std::string network;
    double price_usdc = 0.0;
    std::uint64_t expires_at_unix = 0;

    bool operator==(const IssuedQuote& other) const = default;
};

struct VerifiedPaymentRecord {
    std::string tx_hash;
    std::uint64_t expires_at_unix = 0;
    std::string endpoint_path;
    std::string wallet_address;
    std::string network;
    std::string reference;
    double price_usdc = 0.0;

    bool operator==(const VerifiedPaymentRecord& other) const = default;
};

struct PrepaidBalance {
    std::string wallet_address;
    double balance_usdc = 0.0;

    bool operator==(const PrepaidBalance& other) const = default;
};

struct SubmittedAuthorization {
    std::string nonce;
    std::string tx_hash;

    bool operator==(const SubmittedAuthorization& other) const = default;
};

struct PersistedGatewayState {
    std::vector<gateway::models::RefundableTransaction> refunds;
    std::vector<PendingVerification> pending;
    std::vector<std::string> seen_transactions;
    std::vector<VerifiedPaymentRecord> verified_until;
    std::vector<IssuedQuote> quotes;
    std::vector<PrepaidBalance> credits;
    std::vector<SubmittedAuthorization> authorizations;

    bool operator==(const PersistedGatewayState& other) const = default;
};

[[nodiscard]] std::string serializeGatewayState(const PersistedGatewayState& state);
[[nodiscard]] PersistedGatewayState parseGatewayState(std::string_view json_text);

}  // namespace gateway::core
