#pragma once

#include "gateway/core/Configuration.h"

#include <cstdint>
#include <string>

namespace gateway::blockchain {

/**
 * @brief On-chain transaction observed by the blockchain connector.
 *
 * Requirements: 2.2
 */
struct Transaction {
    std::string hash;
    std::string from_address;
    std::string to_address;
    std::string value_wei;
    uint64_t block_number = 0;
    uint64_t timestamp = 0;
    bool is_finalized = false;
    bool execution_succeeded = true;

    bool operator==(const Transaction& other) const = default;
};

/**
 * @brief ERC-721 identity NFT owned by an agent wallet.
 *
 * Requirements: 4.1-4.5
 */
struct IdentityNFT {
    std::string token_id;
    std::string owner_address;
    std::string identity_type;
    uint64_t registration_timestamp = 0;

    bool operator==(const IdentityNFT& other) const = default;
};

/**
 * @brief Reputation metrics stored by the on-chain reputation registry.
 *
 * success_rate and feedback_score are normalized to [0.0, 1.0].
 *
 * Requirements: 5.1-5.3
 */
struct ReputationData {
    std::string wallet_address;
    double success_rate = 0.0;
    double avg_response_time_ms = 0.0;
    double feedback_score = 0.0;
    uint64_t total_requests = 0;
    uint64_t last_updated = 0;

    bool operator==(const ReputationData& other) const = default;
};

}  // namespace gateway::blockchain

namespace gateway::models {

/**
 * @brief Agent identity and payment context forwarded to the backend API.
 *
 * Requirements: 4.5, 7.3
 */
struct AgentMetadata {
    std::string wallet_address;
    std::string identity_token_id;
    std::string identity_type;
    double reputation_score = 0.0;
    uint64_t total_requests = 0;
    gateway::core::Network payment_network = gateway::core::Network::BASE;
    std::string transaction_hash;

    bool operator==(const AgentMetadata& other) const = default;
};

struct RefundableTransaction {
    std::string tx_hash;
    std::string wallet_address;
    double amount_usdc = 0.0;
    double gas_fee_usdc = 0.0;
    double refund_amount_usdc = 0.0;
    gateway::core::Network network = gateway::core::Network::BASE;
    std::string reason;
    uint64_t refund_requested_at = 0;
    bool refund_processed = false;

    bool operator==(const RefundableTransaction& other) const = default;
};

}  // namespace gateway::models
