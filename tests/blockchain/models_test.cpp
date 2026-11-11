#include "gateway/blockchain/models.h"

#include <gtest/gtest.h>

using gateway::blockchain::IdentityNFT;
using gateway::blockchain::ReputationData;
using gateway::blockchain::Transaction;
using gateway::core::Network;
using gateway::models::AgentMetadata;

TEST(BlockchainModelsTest, TransactionRoundTripEquality) {
    Transaction tx;
    tx.hash = "0xabc";
    tx.from_address = "0x1111111111111111111111111111111111111111";
    tx.to_address = "0x2222222222222222222222222222222222222222";
    tx.value_wei = "1000000";
    tx.block_number = 42;
    tx.timestamp = 1'700'000'000;
    tx.is_finalized = true;

    Transaction copy = tx;
    EXPECT_EQ(tx, copy);
    copy.is_finalized = false;
    EXPECT_NE(tx, copy);
}

TEST(BlockchainModelsTest, IdentityAndReputationFields) {
    IdentityNFT nft;
    nft.token_id = "7";
    nft.owner_address = "0x3333333333333333333333333333333333333333";
    nft.identity_type = "trading-agent";
    nft.registration_timestamp = 1'700'000'100;

    ReputationData reputation;
    reputation.wallet_address = nft.owner_address;
    reputation.success_rate = 0.9;
    reputation.avg_response_time_ms = 40.0;
    reputation.feedback_score = 0.8;
    reputation.total_requests = 12;
    reputation.last_updated = nft.registration_timestamp;

    EXPECT_EQ(IdentityNFT{nft}, nft);
    EXPECT_EQ(ReputationData{reputation}, reputation);
    EXPECT_NE(reputation.success_rate, reputation.feedback_score);
}

TEST(BlockchainModelsTest, AgentMetadataCarriesPaymentContext) {
    AgentMetadata meta;
    meta.wallet_address = "0x4444444444444444444444444444444444444444";
    meta.identity_token_id = "7";
    meta.identity_type = "trading-agent";
    meta.reputation_score = 0.82;
    meta.total_requests = 12;
    meta.payment_network = Network::ARBITRUM;
    meta.transaction_hash = "0xdef";

    AgentMetadata copy = meta;
    EXPECT_EQ(meta, copy);
    EXPECT_EQ(meta.payment_network, Network::ARBITRUM);
}
