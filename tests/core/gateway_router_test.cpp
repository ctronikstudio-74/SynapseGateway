#include "gateway/core/GatewayRouter.h"

#include "../support/fake_chain.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

const std::string kWallet = "0x2222222222222222222222222222222222222222";
const std::string kSignature(64, 'a');

class ScriptedBackend final : public gateway::core::BackendClient {
public:
    gateway::core::ForwardResult next{};
    gateway::core::ForwardRequest last{};
    int calls = 0;
    std::chrono::milliseconds delay{0};

    ScriptedBackend() {
        next.kind = gateway::core::ForwardResult::Kind::Ok;
        next.status = 200;
        next.body = "ok";
        next.headers.push_back({"Content-Type", "text/plain"});
    }

    gateway::core::ForwardResult forward(const std::string&,
                                        const gateway::core::ForwardRequest& request,
                                        std::chrono::milliseconds,
                                        std::chrono::milliseconds) override {
        ++calls;
        last = request;
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
        return next;
    }

    std::size_t poolCapacity(const std::string&) const override { return 10; }
    std::uint64_t reusedConnections() const override { return 0; }
};

gateway::core::EndpointConfig endpoint(const std::string& path, double price, std::uint32_t limit, double trust,
                                       bool allow_new) {
    gateway::core::EndpointConfig value;
    value.path_pattern = path;
    value.price_usdc = price;
    value.trust_threshold = trust;
    value.backend_url = "http://127.0.0.1:9";
    value.backend_timeout = std::chrono::seconds{5};
    value.rate_limit_per_minute = limit;
    value.new_agents_allowed = allow_new;
    return value;
}

std::shared_ptr<gateway::core::Configuration> makeConfig() {
    auto config = std::make_shared<gateway::core::Configuration>();
    config->server.listen_address = "127.0.0.1";
    config->server.listen_port = 8080;
    config->server.monitoring_port = 9090;
    config->server.thread_pool_size = 2;
    config->security.bloom_filter_size_mb = 1;
    config->monitoring.log_level = "error";
    config->endpoints.push_back(endpoint("/api/free", 0.0, 1000, 0.0, true));
    config->endpoints.push_back(endpoint("/api/paid", 0.001, 1000, 0.5, true));
    config->endpoints.push_back(endpoint("/api/limited", 0.0, 1, 0.0, true));
    config->endpoints.push_back(endpoint("/api/strict", 0.0, 1000, 0.99, false));
    config->default_endpoint = endpoint("/default", 0.0, 1000, 0.0, true);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.rpc_url = "http://127.0.0.1:1";
    network.identity_registry = "0x0000000000000000000000000000000000000000";
    network.reputation_registry = "0x0000000000000000000000000000000000000000";
    network.validation_registry = "0x0000000000000000000000000000000000000000";
    network.gas_limit = 100000;
    network.enabled = true;
    config->networks.push_back(network);
    return config;
}

void prime(gateway::test::FakeChain& chain) {
    chain.wallet = "0x1111111111111111111111111111111111111111";
    chain.identity.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::IdentityNFT nft;
    nft.token_id = "7";
    nft.owner_address = kWallet;
    nft.identity_type = "agent";
    nft.registration_timestamp = 10;
    chain.identity.nft = nft;
    chain.reputation.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::ReputationData reputation;
    reputation.wallet_address = kWallet;
    reputation.success_rate = 1.0;
    reputation.avg_response_time_ms = 10.0;
    reputation.feedback_score = 1.0;
    reputation.total_requests = 4;
    chain.reputation.reputation = reputation;
    chain.proof.valid = true;
    chain.healthy = true;
    chain.optimal = gateway::core::Network::BASE;
    gateway::blockchain::Transaction tx;
    tx.hash = "0xpaid";
    tx.from_address = kWallet;
    tx.to_address = chain.wallet;
    tx.value_wei = "1000";
    tx.block_number = 3;
    tx.is_finalized = true;
    tx.execution_succeeded = true;
    chain.transaction.status = gateway::blockchain::TxStatus::Found;
    chain.transaction.transaction = tx;
}

gateway::core::ForwardRequest call(const std::string& method, const std::string& path, const std::string& wallet,
                                   const std::string& tx = {}) {
    gateway::core::ForwardRequest request;
    request.method = method;
    request.target = path;
    request.headers.push_back({"Accept", "application/json"});
    request.headers.push_back({"X-Agent-Signature", kSignature});
    request.headers.push_back({"X-Agent-Wallet", wallet});
    request.headers.push_back({"X-Payment-Tx", tx});
    request.headers.push_back({"X-Request-Id", "trace\r\nX-Evil: 1"});
    return request;
}

}  // namespace

TEST(GatewayRouterTest, RejectsMissingSignatureWalletAndEnforcesThePipeline) {
    gateway::monitoring::Logger::initialize("off");
    auto chain = std::make_shared<gateway::test::FakeChain>();
    prime(*chain);
    auto config = makeConfig();
    const auto state = std::filesystem::temp_directory_path() / "agentgw-router-state.json";
    std::filesystem::remove(state);
    auto payment = std::make_shared<gateway::core::PaymentProcessor>(chain, config, state);
    auto identity = std::make_shared<gateway::core::IdentityVerifier>(chain);
    auto reputation = std::make_shared<gateway::core::ReputationChecker>(chain, config);
    auto backend = std::make_shared<ScriptedBackend>();
    gateway::core::GatewayRouter router(config, payment, identity, reputation, chain, backend);

    auto missing_signature = call("GET", "/api/free", kWallet);
    missing_signature.headers.erase(missing_signature.headers.begin() + 1);
    EXPECT_EQ(router.handle(missing_signature).status, 401);

    auto missing_wallet = call("GET", "/api/free", kWallet);
    missing_wallet.headers.erase(missing_wallet.headers.begin() + 2);
    EXPECT_EQ(router.handle(missing_wallet).status, 401);

    const auto health = router.handle(gateway::core::ForwardRequest{"GET", "/health", {}, {}});
    EXPECT_EQ(health.status, 200);

    const auto free_call = router.handle(call("GET", "/api/free", kWallet));
    EXPECT_EQ(free_call.status, 200);
    EXPECT_EQ(free_call.body, "ok");
    bool saw_payment = false;
    bool saw_reputation = false;
    bool saw_request_id = false;
    for (const auto& header : backend->last.headers) {
        saw_payment = saw_payment || header.name == "X-Payment-Tx";
        saw_reputation = saw_reputation || header.name == "X-Agent-Reputation";
        if (header.name == "X-Request-Id") {
            saw_request_id = header.value.find('\n') == std::string::npos && header.value.find("trace") != std::string::npos;
        }
    }
    EXPECT_FALSE(saw_payment);
    EXPECT_TRUE(saw_reputation);
    EXPECT_TRUE(saw_request_id);
    bool billing = false;
    for (const auto& header : free_call.headers) {
        if (header.name == "X-Payment-Amount") {
            billing = true;
        }
    }
    EXPECT_TRUE(billing);

    const auto unpaid = router.handle(call("GET", "/api/paid", kWallet));
    EXPECT_EQ(unpaid.status, 402);
    EXPECT_NE(unpaid.body.find("0.001"), std::string::npos);

    const auto paid = router.handle(call("POST", "/api/paid", kWallet, "0xpaid"));
    EXPECT_EQ(paid.status, 200);

    auto clock = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
    router.setTimeSource([clock] { return *clock; });
    *clock += std::chrono::hours{25};
    const auto replay = router.handle(call("POST", "/api/paid", kWallet, "0xpaid"));
    EXPECT_EQ(replay.status, 409);

    chain->identity.status = gateway::blockchain::LookupStatus::NotFound;
    chain->identity.nft.reset();
    const auto no_identity = router.handle(call("GET", "/api/free", "0x4444444444444444444444444444444444444444"));
    EXPECT_EQ(no_identity.status, 403);
    chain->identity.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::IdentityNFT nft;
    nft.token_id = "7";
    nft.owner_address = kWallet;
    nft.identity_type = "agent";
    nft.registration_timestamp = 10;
    chain->identity.nft = nft;

    chain->reputation.reputation->success_rate = 0.0;
    chain->reputation.reputation->feedback_score = 0.0;
    const auto low_trust = router.handle(call("GET", "/api/strict", "0x5555555555555555555555555555555555555555"));
    EXPECT_EQ(low_trust.status, 403);

    const std::string limited_wallet = "0x6666666666666666666666666666666666666666";
    const auto first_limited = router.handle(call("GET", "/api/limited", limited_wallet));
    const auto second_limited = router.handle(call("GET", "/api/limited", limited_wallet));
    EXPECT_EQ(first_limited.status, 200);
    EXPECT_EQ(second_limited.status, 429);
    bool retry = false;
    for (const auto& header : second_limited.headers) {
        retry = retry || header.name == "Retry-After";
    }
    EXPECT_TRUE(retry);

    backend->next.kind = gateway::core::ForwardResult::Kind::ConnectionFailed;
    const auto bad_gateway = router.handle(call("POST", "/api/paid", kWallet, "0xdown"));
    EXPECT_EQ(bad_gateway.status, 502);
    EXPECT_TRUE(payment->findRefund("0xdown").has_value());

    backend->next.kind = gateway::core::ForwardResult::Kind::Timeout;
    const auto timeout = router.handle(call("POST", "/api/paid", kWallet, "0xtimeout"));
    EXPECT_EQ(timeout.status, 504);
    EXPECT_TRUE(payment->findRefund("0xtimeout").has_value());

    backend->next.kind = gateway::core::ForwardResult::Kind::Ok;
    backend->next.status = 503;
    backend->next.body = "down";
    const auto upstream = router.handle(call("POST", "/api/paid", kWallet, "0xupstream"));
    EXPECT_EQ(upstream.status, 503);
    EXPECT_TRUE(payment->findRefund("0xupstream").has_value());

    auto raised = makeConfig();
    raised->endpoints[1].price_usdc = 0.5;
    router.applyConfiguration(raised);
    const auto requoted = router.handle(call("GET", "/api/paid", kWallet));
    EXPECT_EQ(requoted.status, 402);
    EXPECT_NE(requoted.body.find("0.5"), std::string::npos);

    backend->delay = std::chrono::milliseconds{200};
    backend->next.status = 200;
    backend->next.kind = gateway::core::ForwardResult::Kind::Ok;
    std::thread in_flight([&] {
        const auto response = router.handle(call("GET", "/api/free", kWallet));
        EXPECT_TRUE(response.status == 200 || response.status == 503);
    });
    for (int attempt = 0; attempt < 50 && router.inFlight() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    router.shutdown(std::chrono::seconds{2});
    in_flight.join();
    EXPECT_EQ(router.inFlight(), 0u);
    EXPECT_FALSE(router.isAccepting());
    const auto rejected = router.handle(call("GET", "/api/free", kWallet));
    EXPECT_EQ(rejected.status, 503);

    gateway::core::PaymentProcessor restored(chain, config, state);
    const auto pending = restored.pendingVerifications();
    EXPECT_FALSE(pending.empty());
    std::filesystem::remove(state);
}
