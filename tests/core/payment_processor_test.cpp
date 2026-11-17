#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/request_signature.h"
#include "gateway/blockchain/settlement.h"
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

void bindQuote(gateway::core::PaymentProcessor& processor, gateway::core::PaymentRequest& request) {
    const auto quote = processor.requestPayment(request);
    ASSERT_TRUE(quote.transaction_reference);
    request.tx_reference = *quote.transaction_reference;
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
    bindQuote(processor, request);

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
    processor.updateConfiguration(configWithPrice(0.001));
    request.endpoint_path = "/api/v1/chat";
    request.tx_hash = "0xshort";
    bindQuote(processor, request);
    const auto short_payment = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(short_payment.status, gateway::core::PaymentResponse::Status::INSUFFICIENT_AMOUNT);
}

TEST(PaymentProcessorTest, RequiresConfiguredUsdcFromTheAgentWallet) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    auto config = configWithPrice(0.001);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.rpc_url = "https://rpc.example/base";
    network.usdc_contract = "0x00000000000000000000000000000000000000aa";
    network.usdc_decimals = 6;
    config->networks.push_back(network);

    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    chain->transaction.transaction = paid(chain->wallet, "1000");
    chain->transaction.transaction->token_contract = "0x00000000000000000000000000000000000000bb";
    gateway::core::PaymentProcessor processor(chain, config, {});
    gateway::core::PaymentRequest request;
    request.endpoint_path = "/api/v1/chat";
    request.wallet_address = "0x2222222222222222222222222222222222222222";
    request.tx_hash = "0xabc";
    bindQuote(processor, request);

    const auto foreign = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(foreign.status, gateway::core::PaymentResponse::Status::INVALID_TRANSACTION);
    EXPECT_NE(foreign.message.find("configured stablecoin"), std::string::npos);

    chain->transaction.transaction->token_contract = network.usdc_contract;
    chain->transaction.transaction->from_address = "0x3333333333333333333333333333333333333333";
    chain->transaction.transaction->hash = "0xdef";
    request.tx_hash = "0xdef";
    const auto other_sender = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(other_sender.status, gateway::core::PaymentResponse::Status::INVALID_TRANSACTION);
    EXPECT_NE(other_sender.message.find("agent wallet"), std::string::npos);

    chain->transaction.transaction->from_address = request.wallet_address;
    chain->transaction.transaction->hash = "0xgood";
    request.tx_hash = "0xgood";
    const auto accepted = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(accepted.status, gateway::core::PaymentResponse::Status::VERIFIED);

    network.usdc_decimals = 18;
    config->networks[0] = network;
    processor.updateConfiguration(config);
    chain->transaction.transaction->value_wei = "1000000000000000";
    chain->transaction.transaction->hash = "0xbnb";
    request.tx_hash = "0xbnb";
    const auto eighteen = processor.verifyPayment(request, 0.001);
    EXPECT_EQ(eighteen.status, gateway::core::PaymentResponse::Status::VERIFIED);
}

TEST(PaymentProcessorTest, HashPaysOnlyTheQuotedPathDuringTheCacheWindow) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    chain->transaction.transaction = paid(chain->wallet, "1000");
    gateway::core::PaymentProcessor processor(chain, configWithPrice(0.001), {});
    const auto origin = std::chrono::steady_clock::time_point{};

    gateway::core::PaymentRequest first;
    first.endpoint_path = "/api/v1/chat";
    first.wallet_address = "0x2222222222222222222222222222222222222222";
    first.tx_hash = "0xabc";
    const auto issued = processor.requestPayment(first, origin);
    ASSERT_TRUE(issued.transaction_reference);
    ASSERT_TRUE(issued.expires_at_unix);
    first.tx_reference = *issued.transaction_reference;
    EXPECT_EQ(processor.verifyPayment(first, 0.001, origin).status, gateway::core::PaymentResponse::Status::VERIFIED);

    gateway::core::PaymentRequest other = first;
    other.endpoint_path = "/api/v1/other";
    const auto second = processor.requestPayment(other, origin);
    ASSERT_TRUE(second.transaction_reference);
    other.tx_reference = *second.transaction_reference;
    const auto rebound = processor.verifyPayment(other, 0.001, origin + std::chrono::minutes{1});
    EXPECT_EQ(rebound.status, gateway::core::PaymentResponse::Status::INVALID_TRANSACTION);
    EXPECT_NE(rebound.message.find("another quote"), std::string::npos);
    EXPECT_EQ(processor.verifyPayment(first, 0.001, origin + std::chrono::minutes{1}).status,
              gateway::core::PaymentResponse::Status::VERIFIED);

    other.tx_hash = "0xfresh";
    chain->transaction.transaction->hash = "0xfresh";
    const auto expired = processor.verifyPayment(other, 0.001, origin + std::chrono::minutes{16});
    EXPECT_EQ(expired.status, gateway::core::PaymentResponse::Status::INVALID_TRANSACTION);
    EXPECT_NE(expired.message.find("expired"), std::string::npos);
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

TEST(PaymentProcessorTest, AuthorizationBroadcastIsVerifiedOnce) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    auto config = configWithPrice(0.001);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.usdc_contract = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    network.usdc_decimals = 6;
    config->networks.push_back(network);
    const std::string key(64, '1');
    const auto agent = gateway::blockchain::addressFromPrivateKey("0x" + key);
    ASSERT_TRUE(agent);
    const auto path = std::filesystem::temp_directory_path() / "agentgw-authorization.json";
    std::filesystem::remove(path);
    gateway::core::PaymentProcessor processor(chain, config, path);
    gateway::core::PaymentRequest request;
    request.endpoint_path = "/api/v1/chat";
    request.wallet_address = *agent;
    bindQuote(processor, request);

    gateway::blockchain::TransferAuthorization authorization;
    authorization.from = *agent;
    authorization.to = chain->wallet;
    authorization.value_units = "1000";
    authorization.valid_after = 0;
    authorization.valid_before = 4'000'000'000ULL;
    authorization.nonce_hex = "0x" + std::string(64, 'a');
    const auto digest = gateway::blockchain::transferAuthorizationDigest(
        authorization, "USD Coin", "2", gateway::blockchain::chainIdOf(gateway::core::Network::BASE), network.usdc_contract);
    const auto signature = gateway::blockchain::signDigest("0x" + key, digest);
    ASSERT_TRUE(signature);
    authorization.signature_hex = "0x" + gateway::blockchain::toHex(*signature);
    chain->authorization_broadcast.status = gateway::blockchain::Broadcast::Status::Submitted;
    chain->authorization_broadcast.hash = "0xsettled";
    auto tx = paid(chain->wallet, "1000");
    tx.hash = "0xsettled";
    tx.from_address = *agent;
    tx.token_contract = network.usdc_contract;
    chain->transaction = gateway::blockchain::TxLookup{gateway::blockchain::TxStatus::Found, tx};

    const auto settled = processor.settleAuthorization(request, authorization, 0.001);
    EXPECT_EQ(settled.status, gateway::core::PaymentResponse::Status::VERIFIED);
    const auto replay = processor.settleAuthorization(request, authorization, 0.001);
    EXPECT_EQ(replay.status, gateway::core::PaymentResponse::Status::VERIFIED);
    std::filesystem::remove(path);
}

TEST(PaymentProcessorTest, OnChainRefundClosesOnlyAfterTheReceipt) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    auto config = configWithPrice(1.0);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.usdc_contract = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    network.usdc_decimals = 6;
    config->networks.push_back(network);
    config->treasury_private_key = "0x" + std::string(64, '2');
    const auto path = std::filesystem::temp_directory_path() / "agentgw-onchain-refund.json";
    std::filesystem::remove(path);
    gateway::core::PaymentProcessor processor(chain, config, path);
    processor.markRefundable("0xpaid", chain->wallet, 1.0, "backend_error");
    chain->transfer_broadcast.status = gateway::blockchain::Broadcast::Status::Submitted;
    chain->transfer_broadcast.hash = "0xrefund";
    EXPECT_FALSE(processor.processRefund("0xpaid", chain->wallet, 0.25));
    const auto pending = processor.findRefund("0xpaid");
    ASSERT_TRUE(pending);
    EXPECT_FALSE(pending->refund_processed);
    EXPECT_EQ(pending->refund_tx_hash, "0xrefund");

    gateway::blockchain::Transaction tx;
    tx.value_wei = "750000";
    tx.is_finalized = true;
    tx.execution_succeeded = true;
    chain->token_transfer = gateway::blockchain::TxLookup{gateway::blockchain::TxStatus::Found, tx};
    chain->script_token_transfer = true;
    EXPECT_TRUE(processor.processRefund("0xpaid", chain->wallet, 0.25));
    const auto closed = processor.findRefund("0xpaid");
    ASSERT_TRUE(closed);
    EXPECT_TRUE(closed->refund_processed);
    std::filesystem::remove(path);
}

TEST(PaymentProcessorTest, PrepaidDepositIsDebitedOnce) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    auto config = configWithPrice(0.001);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.usdc_contract = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    network.usdc_decimals = 6;
    config->networks.push_back(network);
    const auto path = std::filesystem::temp_directory_path() / "agentgw-credits.json";
    std::filesystem::remove(path);
    gateway::core::PaymentProcessor processor(chain, config, path);
    const std::string wallet = "0x2222222222222222222222222222222222222222";
    auto tx = paid(chain->wallet, "1000000");
    tx.from_address = wallet;
    tx.token_contract = network.usdc_contract;
    chain->transaction = gateway::blockchain::TxLookup{gateway::blockchain::TxStatus::Found, tx};
    const auto credited = processor.creditDeposit(wallet, "0xdeposit");
    EXPECT_EQ(credited.status, gateway::core::PaymentResponse::Status::VERIFIED);
    EXPECT_NEAR(processor.prepaidBalance(wallet), 1.0, 1e-9);
    EXPECT_EQ(processor.creditDeposit(wallet, "0xdeposit").status, gateway::core::PaymentResponse::Status::REPLAY_DETECTED);
    EXPECT_TRUE(processor.tryDebitPrepaid(wallet, 0.4));
    EXPECT_FALSE(processor.tryDebitPrepaid(wallet, 0.7));
    EXPECT_NEAR(processor.prepaidBalance(wallet), 0.6, 1e-9);
    std::filesystem::remove(path);
}

TEST(PaymentProcessorTest, ExtraStablecoinSettlesAtItsOwnDecimals) {
    auto chain = std::make_shared<gateway::test::FakeChain>();
    auto config = configWithPrice(0.001);
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.usdc_contract = "0x00000000000000000000000000000000000000aa";
    network.usdc_decimals = 6;
    network.stablecoins.push_back(gateway::core::StablecoinConfig{
        "EURC", "0x00000000000000000000000000000000000000cc", 6});
    config->networks.push_back(network);
    gateway::core::PaymentProcessor processor(chain, config, {});
    gateway::core::PaymentRequest request;
    request.endpoint_path = "/api/v1/chat";
    request.wallet_address = "0x2222222222222222222222222222222222222222";
    request.tx_hash = "0xeurc";
    bindQuote(processor, request);
    auto tx = paid(chain->wallet, "1000");
    tx.hash = "0xeurc";
    tx.from_address = request.wallet_address;
    tx.token_contract = "0x00000000000000000000000000000000000000cc";
    chain->transaction = gateway::blockchain::TxLookup{gateway::blockchain::TxStatus::Found, tx};
    EXPECT_EQ(processor.verifyPayment(request, 0.001).status, gateway::core::PaymentResponse::Status::VERIFIED);
}
