#include "gateway/core/IdentityVerifier.h"
#include "gateway/core/ReputationChecker.h"

#include "../support/fake_chain.h"
#include "../support/property.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>

namespace {

std::shared_ptr<gateway::core::Configuration> configFor(bool allow_new, double threshold,
                                                        std::vector<gateway::core::ProofType> proofs) {
    auto config = std::make_shared<gateway::core::Configuration>();
    gateway::core::EndpointConfig endpoint;
    endpoint.path_pattern = "/api/v1/chat";
    endpoint.price_usdc = 0.0;
    endpoint.trust_threshold = threshold;
    endpoint.required_proofs = std::move(proofs);
    endpoint.backend_url = "http://localhost";
    endpoint.backend_timeout = std::chrono::seconds{5};
    endpoint.rate_limit_per_minute = 10;
    endpoint.new_agents_allowed = allow_new;
    config->endpoints.push_back(endpoint);
    config->default_endpoint = endpoint;
    return config;
}

}  // namespace

TEST(WalletExtractionProperty, ReadsAddressFromAnySupportedSource) {
    const std::string address = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    gateway::test::checkProperty("wallet extraction", [&](std::mt19937& rng) {
        std::uniform_int_distribution<int> source(0, 3);
        gateway::core::WalletSource request;
        switch (source(rng)) {
            case 0:
                request.headers.emplace_back("X-Agent-Wallet", address);
                break;
            case 1:
                request.headers.emplace_back("x-wallet-address", address);
                break;
            case 2:
                request.headers.emplace_back("X-Payment-Wallet", address);
                break;
            default:
                request.transaction_from = address;
                break;
        }
        const auto extracted = gateway::core::IdentityVerifier::extractWalletAddress(request);
        ASSERT_TRUE(extracted);
        EXPECT_EQ(*extracted, address);
    });
}

TEST(IdentityVerifierTest, CachesNftMetadata) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    gateway::blockchain::IdentityNFT nft;
    nft.token_id = "9";
    nft.owner_address = chain->wallet;
    nft.identity_type = "audit-agent";
    nft.registration_timestamp = 42;
    chain->identity.status = gateway::blockchain::LookupStatus::Found;
    chain->identity.nft = nft;
    gateway::core::IdentityVerifier verifier(chain);
    const auto first = verifier.verifyIdentity(chain->wallet, gateway::core::Network::BASE);
    const auto second = verifier.verifyIdentity(chain->wallet, gateway::core::Network::BASE);
    EXPECT_EQ(first.status, gateway::core::IdentityVerificationResult::Status::VERIFIED);
    EXPECT_EQ(second.message, "cached identity");
    EXPECT_EQ(chain->identity_queries, 1);
    ASSERT_TRUE(second.identity_data);
    EXPECT_EQ(second.identity_data->identity_type, "audit-agent");
    EXPECT_EQ(second.identity_data->registration_timestamp, 42u);
}

TEST(TrustScoreProperty, WeightedSumStaysInsideUnitInterval) {
    gateway::test::checkProperty("trust score", [](std::mt19937& rng) {
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::uniform_real_distribution<double> millis(0.0, 5000.0);
        std::uniform_real_distribution<double> weight(0.0, 1.0);
        gateway::blockchain::ReputationData data;
        data.success_rate = unit(rng);
        data.avg_response_time_ms = millis(rng);
        data.feedback_score = unit(rng);
        gateway::core::TrustWeights weights{weight(rng), weight(rng), weight(rng)};
        const double response = gateway::core::responseTimeScore(data.avg_response_time_ms);
        const double weight_sum =
            weights.success_rate_weight + weights.response_time_weight + weights.feedback_weight;
        const double expected = weight_sum == 0.0
                                    ? 0.0
                                    : (weights.success_rate_weight * data.success_rate +
                                       weights.response_time_weight * response +
                                       weights.feedback_weight * data.feedback_score) /
                                          weight_sum;
        const double actual = gateway::core::calculateTrustScore(data, weights);
        EXPECT_NEAR(actual, expected, 1e-12);
        EXPECT_GE(actual, 0.0);
        EXPECT_LE(actual, 1.0);
    });
}

TEST(ReputationCheckerTest, NewAgentPolicyAndProofTimeout) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    chain->reputation.status = gateway::blockchain::LookupStatus::NotFound;
    gateway::core::ReputationChecker allowed(chain, configFor(true, 0.7, {}));
    const auto admitted = allowed.checkReputation(chain->wallet, gateway::core::Network::BASE, "/api/v1/chat");
    EXPECT_EQ(admitted.status, gateway::core::ReputationCheckResult::Status::APPROVED);

    gateway::core::ReputationChecker rejected(chain, configFor(false, 0.7, {}));
    const auto denied = rejected.checkReputation(chain->wallet, gateway::core::Network::BASE, "/api/v1/chat");
    EXPECT_EQ(denied.status, gateway::core::ReputationCheckResult::Status::NO_REPUTATION_DATA);

    chain->reputation.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::ReputationData data;
    data.success_rate = 0.95;
    data.avg_response_time_ms = 20;
    data.feedback_score = 0.9;
    data.total_requests = 10;
    chain->reputation.reputation = data;
    chain->proof.valid = true;
    chain->proof.elapsed = std::chrono::milliseconds{250};
    chain->proof.detail = "slow";
    gateway::core::ReputationChecker timed(chain, configFor(false, 0.2, {gateway::core::ProofType::ZK_ML}));
    const auto timeout = timed.checkReputation(chain->wallet, gateway::core::Network::BASE, "/api/v1/chat");
    EXPECT_EQ(timeout.status, gateway::core::ReputationCheckResult::Status::PROOF_VALIDATION_FAILED);
    EXPECT_NE(timeout.message.find("200ms"), std::string::npos);
}
