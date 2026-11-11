#include "gateway/core/GatewayRouter.h"

#include "../support/fake_chain.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace {

class InstantBackend final : public gateway::core::BackendClient {
public:
    gateway::core::ForwardResult forward(const std::string&,
                                        const gateway::core::ForwardRequest&,
                                        std::chrono::milliseconds,
                                        std::chrono::milliseconds) override {
        gateway::core::ForwardResult result;
        result.kind = gateway::core::ForwardResult::Kind::Ok;
        result.status = 200;
        result.body = "ok";
        return result;
    }
    std::size_t poolCapacity(const std::string&) const override { return 10; }
    std::uint64_t reusedConnections() const override { return 0; }
};

}  // namespace

TEST(PerformanceBenchmark, VerificationStaysInsideTheLatencyBudget) {
    gateway::monitoring::Logger::initialize("off");
    auto chain = std::make_shared<gateway::test::FakeChain>();
    chain->wallet = "0x1111111111111111111111111111111111111111";
    chain->identity.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::IdentityNFT nft;
    nft.token_id = "1";
    nft.owner_address = "0x2222222222222222222222222222222222222222";
    nft.identity_type = "agent";
    chain->identity.nft = nft;
    chain->reputation.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::ReputationData reputation;
    reputation.success_rate = 1;
    reputation.feedback_score = 1;
    reputation.total_requests = 1;
    chain->reputation.reputation = reputation;
    chain->optimal = gateway::core::Network::BASE;

    auto config = std::make_shared<gateway::core::Configuration>();
    config->security.bloom_filter_size_mb = 1;
    config->monitoring.log_level = "error";
    gateway::core::EndpointConfig endpoint;
    endpoint.path_pattern = "/api/free";
    endpoint.price_usdc = 0.0;
    endpoint.trust_threshold = 0.0;
    endpoint.backend_url = "http://127.0.0.1:9";
    endpoint.backend_timeout = std::chrono::seconds{5};
    endpoint.rate_limit_per_minute = 1000000;
    endpoint.new_agents_allowed = true;
    config->endpoints.push_back(endpoint);
    config->default_endpoint = endpoint;

    auto payment = std::make_shared<gateway::core::PaymentProcessor>(chain, config, std::filesystem::path{});
    auto identity = std::make_shared<gateway::core::IdentityVerifier>(chain);
    auto reputation_checker = std::make_shared<gateway::core::ReputationChecker>(chain, config);
    gateway::core::GatewayRouter router(config, payment, identity, reputation_checker, chain,
                                       std::make_shared<InstantBackend>());

    gateway::core::ForwardRequest request;
    request.method = "GET";
    request.target = "/api/free";
    request.headers.push_back({"X-Agent-Signature", std::string(64, 'b')});
    request.headers.push_back({"X-Agent-Wallet", nft.owner_address});

    constexpr int kRequests = 2000;
    std::vector<double> millis;
    millis.reserve(kRequests);
    const auto started = std::chrono::steady_clock::now();
    for (int index = 0; index < kRequests; ++index) {
        const auto one = std::chrono::steady_clock::now();
        const auto response = router.handle(request);
        const auto elapsed = std::chrono::steady_clock::now() - one;
        ASSERT_EQ(response.status, 200);
        millis.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double throughput = static_cast<double>(kRequests) / seconds;
    std::sort(millis.begin(), millis.end());
    const double p95 = millis[static_cast<std::size_t>(kRequests * 95 / 100)];
    std::cout << "verification throughput " << throughput << " req/s, p95 " << p95 << " ms" << std::endl;
#ifdef NDEBUG
    const double minimum = 1000.0;
#else
    const double minimum = 200.0;
#endif
    EXPECT_GE(throughput, minimum);
    EXPECT_LT(p95, 100.0);

    gateway::core::PaymentRequest paid;
    paid.endpoint_path = "/api/free";
    paid.wallet_address = nft.owner_address;
    paid.tx_hash = "0xcache";
    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    gateway::blockchain::Transaction tx;
    tx.to_address = chain->wallet;
    tx.from_address = nft.owner_address;
    tx.value_wei = "1000";
    tx.is_finalized = true;
    chain->transaction.transaction = tx;
    ASSERT_EQ(payment->verifyPayment(paid, 0.001).status, gateway::core::PaymentResponse::Status::VERIFIED);
    const int queries_after_first = chain->transaction_queries;
    for (int index = 0; index < 1000; ++index) {
        ASSERT_EQ(payment->verifyPayment(paid, 0.001).status, gateway::core::PaymentResponse::Status::VERIFIED);
    }
    EXPECT_EQ(chain->transaction_queries, queries_after_first);
}
