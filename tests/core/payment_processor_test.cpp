#include "gateway/core/PaymentProcessor.h"

#include "../support/fake_chain.h"
#include "../support/property.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

std::shared_ptr<gateway::core::Configuration> configWithPrice(double price) {
    auto config = std::make_shared<gateway::core::Configuration>();
    config->server.listen_address = "127.0.0.1";
    config->server.listen_port = 8080;
    config->server.monitoring_port = 9090;
    config->security.bloom_filter_size_mb = 1;
    config->security.bloom_filter_retention = std::chrono::hours{24};
    gateway::core::EndpointConfig endpoint;
    endpoint.path_pattern = "/api/v1/chat";
    endpoint.price_usdc = price;
    endpoint.trust_threshold = 0.5;
    endpoint.backend_url = "http://localhost:3000";
    endpoint.backend_timeout = std::chrono::seconds{5};
    endpoint.rate_limit_per_minute = 10;
    endpoint.new_agents_allowed = true;
    config->endpoints.push_back(endpoint);
    config->default_endpoint = endpoint;
    config->monitoring.log_level = "info";
    return config;
}

gateway::blockchain::Transaction paid(const std::string& wallet, const std::string& units) {
    gateway::blockchain::Transaction tx;
    tx.hash = "0xabc";
    tx.from_address = "0x2222222222222222222222222222222222222222";
    tx.to_address = wallet;
    tx.value_wei = units;
    tx.block_number = 10;
    tx.is_finalized = true;
    tx.execution_succeeded = true;
    return tx;
}

}  // namespace

TEST(PaymentAmountProperty, AcceptsWithinOneTenthPercent) {
    gateway::test::checkProperty("amount tolerance", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> quoted_units(0, 5'000'000);
        std::uniform_int_distribution<int> delta(-50, 5000);
        const int quoted = quoted_units(rng);
        const int paid = std::max(0, quoted + delta(rng));
        const double quoted_usdc = static_cast<double>(quoted) / 1000000.0;
        const double paid_usdc = static_cast<double>(paid) / 1000000.0;
        const bool expected = paid >= (quoted * 999) / 1000;
        EXPECT_EQ(gateway::core::amountCoversQuote(paid_usdc, quoted_usdc), expected);
    });
}

TEST(PaymentProcessorTest, ReferenceIdsAreUniqueAcrossThreads) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    gateway::core::PaymentProcessor processor(chain, configWithPrice(0.001), {});
    std::mutex mutex;
    std::set<std::string> references;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 50; ++i) {
                const auto response = processor.requestPayment(gateway::core::PaymentRequest{"/api/v1/chat", "", {}, {}, 0});
                ASSERT_TRUE(response.transaction_reference);
                std::lock_guard lock(mutex);
                references.insert(*response.transaction_reference);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(references.size(), 200u);
}

TEST(PaymentProcessorTest, VerifiesOnceThenCachesThenRejectsReplay) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    chain->transaction.transaction = paid(chain->wallet, "1000");
    gateway::core::PaymentProcessor processor(chain, configWithPrice(0.001), {});
    gateway::core::PaymentRequest request;
    request.endpoint_path = "/api/v1/chat";
    request.tx_hash = "0xabc";
    const auto origin = std::chrono::steady_clock::time_point{};

    const auto first = processor.verifyPayment(request, 0.001, origin);
    EXPECT_EQ(first.status, gateway::core::PaymentResponse::Status::VERIFIED);
    EXPECT_EQ(chain->transaction_queries, 1);

    const auto cached = processor.verifyPayment(request, 0.001, origin + std::chrono::hours{1});
    EXPECT_EQ(cached.status, gateway::core::PaymentResponse::Status::VERIFIED);
    EXPECT_EQ(cached.message, "cached payment verification");
    EXPECT_EQ(chain->transaction_queries, 1);

    const auto replay = processor.verifyPayment(request, 0.001, origin + std::chrono::hours{25});
    EXPECT_EQ(replay.status, gateway::core::PaymentResponse::Status::REPLAY_DETECTED);
    EXPECT_EQ(chain->transaction_queries, 1);
}

TEST(PaymentProcessorTest, ZeroPriceSkipsTheChainAndShortPaymentIsRejected) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    gateway::core::PaymentProcessor processor(chain, configWithPrice(0.0), {});
    gateway::core::PaymentRequest request;
    const auto free = processor.verifyPayment(request, 0.0);
    EXPECT_EQ(free.status, gateway::core::PaymentResponse::Status::VERIFIED);
    EXPECT_EQ(chain->transaction_queries, 0);

    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    chain->transaction.transaction = paid(chain->wallet, "500");
    request.tx_hash = "0xshort";
    const auto short_payment = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(short_payment.status, gateway::core::PaymentResponse::Status::INSUFFICIENT_AMOUNT);
}

TEST(PaymentProcessorTest, RefundIsBlockedWhenGasConsumesThePayment) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    const auto path = std::filesystem::temp_directory_path() / "agentgw-refunds.json";
    std::filesystem::remove(path);
    gateway::core::PaymentProcessor processor(chain, configWithPrice(0.001), path);
    processor.markRefundable("0xabc", chain->wallet, 1.0, "backend_error");
    EXPECT_FALSE(processor.processRefund("0xabc", chain->wallet, 1.0));
    EXPECT_TRUE(processor.processRefund("0xabc", chain->wallet, 0.25));
    const auto stored = processor.findRefund("0xabc");
    ASSERT_TRUE(stored);
    EXPECT_TRUE(stored->refund_processed);
    EXPECT_NEAR(stored->refund_amount_usdc, 0.75, 1e-12);

    gateway::core::PaymentProcessor restored(chain, configWithPrice(0.001), path);
    const auto loaded = restored.findRefund("0xabc");
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->refund_processed);
    std::filesystem::remove(path);
}
