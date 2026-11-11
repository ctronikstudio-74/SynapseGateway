#include "gateway/blockchain/BlockchainConnector.h"
#include "gateway/blockchain/network_monitor.h"
#include "gateway/core/GatewayRouter.h"
#include "gateway/http/HttpServer.h"

#include "../support/fake_chain.h"
#include "../support/local_http.h"

#include <gtest/gtest.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

const std::string kWallet = "0x2222222222222222222222222222222222222222";
const std::string kSignature(64, 'a');

struct RawResponse {
    int status = 0;
    std::string body;
};

RawResponse request(std::uint16_t port, const std::string& method, const std::string& target, const std::string& body,
                   const std::vector<std::pair<std::string, std::string>>& headers) {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
    stream.connect(endpoints);
    http::request<http::string_body> outbound{http::string_to_verb(method), target, 11};
    outbound.set(http::field::host, "127.0.0.1");
    outbound.set(http::field::user_agent, "SynapseGatewayTest");
    for (const auto& header : headers) {
        outbound.set(header.first, header.second);
    }
    outbound.body() = body;
    outbound.prepare_payload();
    http::write(stream, outbound);
    beast::flat_buffer buffer;
    http::response<http::string_body> inbound;
    http::read(stream, buffer, inbound);
    RawResponse response;
    response.status = inbound.result_int();
    response.body = inbound.body();
    beast::error_code error;
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    return response;
}

bool waitForPort(std::uint16_t port) {
    using tcp = boost::asio::ip::tcp;
    for (int attempt = 0; attempt < 100; ++attempt) {
        boost::asio::io_context ioc;
        tcp::socket socket(ioc);
        boost::system::error_code error;
        socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), error);
        if (!error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    return false;
}

std::uint16_t freePort() {
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    return acceptor.local_endpoint().port();
}

gateway::core::BlockchainNetworkConfig networkConfig(gateway::core::Network network, std::uint16_t port) {
    gateway::core::BlockchainNetworkConfig value;
    value.network = network;
    value.rpc_url = "http://127.0.0.1:" + std::to_string(port);
    value.identity_registry = "0x0000000000000000000000000000000000000000";
    value.reputation_registry = "0x0000000000000000000000000000000000000000";
    value.validation_registry = "0x0000000000000000000000000000000000000000";
    value.gas_limit = 21000;
    value.enabled = true;
    return value;
}

std::string rpcResult(const std::string& body, const std::string& gas_hex) {
    if (body.find("eth_gasPrice") != std::string::npos) {
        return std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"") + gas_hex + "\"}";
    }
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x10\"}";
}

}  // namespace

TEST(GatewayFlow, NetworkFailoverDropsUnhealthyChains) {
    std::atomic<bool> base_up{true};
    gateway::test::LocalHttpServer base([&](const gateway::test::LocalRequest& incoming) {
        gateway::test::LocalResponse response;
        response.content_type = "application/json";
        if (!base_up.load()) {
            response.status = 500;
            response.body = "{}";
            return response;
        }
        response.body = rpcResult(incoming.body, "0x1");
        return response;
    });
    gateway::test::LocalHttpServer arbitrum([](const gateway::test::LocalRequest& incoming) {
        gateway::test::LocalResponse response;
        response.content_type = "application/json";
        response.body = rpcResult(incoming.body, "0x5f5e100");
        return response;
    });

    auto rpc = std::make_shared<gateway::blockchain::HttpRpcClient>(2);
    auto connector = std::make_shared<gateway::blockchain::BlockchainConnector>(
        std::vector<gateway::core::BlockchainNetworkConfig>{networkConfig(gateway::core::Network::BASE, base.port()),
                                                            networkConfig(gateway::core::Network::ARBITRUM, arbitrum.port())},
        "gateway-seed", rpc);
    connector->initialize();
    auto selected = connector->selectOptimalNetwork();
    ASSERT_TRUE(selected);
    EXPECT_EQ(*selected, gateway::core::Network::BASE);

    base_up.store(false);
    gateway::blockchain::NetworkMonitor monitor(connector, std::chrono::milliseconds{50}, std::chrono::milliseconds{50});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline && connector->isNetworkHealthy(gateway::core::Network::BASE)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    EXPECT_FALSE(connector->isNetworkHealthy(gateway::core::Network::BASE));
    selected = connector->selectOptimalNetwork();
    ASSERT_TRUE(selected);
    EXPECT_EQ(*selected, gateway::core::Network::ARBITRUM);

    base_up.store(true);
    const auto recover = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < recover && !connector->isNetworkHealthy(gateway::core::Network::BASE)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    EXPECT_TRUE(connector->isNetworkHealthy(gateway::core::Network::BASE));
}

TEST(GatewayFlow, HttpPipelineCoversPaymentIdentityAndRefunds) {
    std::mutex backend_mutex;
    std::vector<std::pair<std::string, std::string>> last_headers;
    gateway::test::LocalHttpServer backend([&](const gateway::test::LocalRequest& incoming) {
        std::lock_guard lock(backend_mutex);
        last_headers = incoming.headers;
        gateway::test::LocalResponse response;
        response.status = 200;
        response.body = "pong";
        response.content_type = "text/plain";
        return response;
    });

    auto chain = std::make_shared<gateway::test::FakeChain>();
    chain->wallet = "0x1111111111111111111111111111111111111111";
    chain->identity.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::IdentityNFT nft;
    nft.token_id = "9";
    nft.owner_address = kWallet;
    nft.identity_type = "agent";
    chain->identity.nft = nft;
    chain->reputation.status = gateway::blockchain::LookupStatus::Found;
    gateway::blockchain::ReputationData reputation;
    reputation.success_rate = 1;
    reputation.feedback_score = 1;
    reputation.avg_response_time_ms = 5;
    reputation.total_requests = 3;
    chain->reputation.reputation = reputation;
    chain->healthy = true;
    chain->optimal = gateway::core::Network::BASE;

    auto config = std::make_shared<gateway::core::Configuration>();
    config->server.listen_address = "127.0.0.1";
    config->server.listen_port = freePort();
    config->server.monitoring_port = 0;
    config->server.thread_pool_size = 2;
    config->security.bloom_filter_size_mb = 1;
    config->monitoring.log_level = "error";
    gateway::core::EndpointConfig free_endpoint;
    free_endpoint.path_pattern = "/api/free";
    free_endpoint.price_usdc = 0.0;
    free_endpoint.trust_threshold = 0.0;
    free_endpoint.backend_url = "http://127.0.0.1:" + std::to_string(backend.port());
    free_endpoint.backend_timeout = std::chrono::seconds{5};
    free_endpoint.rate_limit_per_minute = 1000;
    free_endpoint.new_agents_allowed = true;
    gateway::core::EndpointConfig paid_endpoint = free_endpoint;
    paid_endpoint.path_pattern = "/api/paid";
    paid_endpoint.price_usdc = 0.001;
    config->endpoints.push_back(free_endpoint);
    config->endpoints.push_back(paid_endpoint);
    config->default_endpoint = free_endpoint;
    gateway::core::BlockchainNetworkConfig network;
    network.network = gateway::core::Network::BASE;
    network.rpc_url = "http://127.0.0.1:1";
    network.identity_registry = "0x0000000000000000000000000000000000000000";
    network.reputation_registry = "0x0000000000000000000000000000000000000000";
    network.validation_registry = "0x0000000000000000000000000000000000000000";
    network.gas_limit = 1000;
    network.enabled = true;
    config->networks.push_back(network);

    auto payment = std::make_shared<gateway::core::PaymentProcessor>(chain, config, std::filesystem::path{});
    auto identity = std::make_shared<gateway::core::IdentityVerifier>(chain);
    auto reputation_checker = std::make_shared<gateway::core::ReputationChecker>(chain, config);
    auto pool = std::make_shared<gateway::core::BackendConnectionPool>();
    gateway::core::GatewayRouter router(config, payment, identity, reputation_checker, chain, pool);
    gateway::http::HttpServer server(router, "127.0.0.1", config->server.listen_port, 2);
    std::thread listener([&] { server.run(); });
    struct StopServer {
        gateway::http::HttpServer& server;
        std::thread& listener;
        ~StopServer() {
            server.requestStop();
            if (listener.joinable()) {
                listener.join();
            }
        }
    } stop{server, listener};
    ASSERT_TRUE(waitForPort(config->server.listen_port));

    const auto health = request(config->server.listen_port, "GET", "/health", "", {});
    EXPECT_EQ(health.status, 200);

    const auto unsigned_call = request(config->server.listen_port, "GET", "/api/free", "", {{"X-Agent-Wallet", kWallet}});
    EXPECT_EQ(unsigned_call.status, 401);

    const std::vector<std::pair<std::string, std::string>> headers = {{"X-Agent-Signature", kSignature},
                                                                      {"X-Agent-Wallet", kWallet},
                                                                      {"Accept", "application/json"}};
    const auto allowed = request(config->server.listen_port, "GET", "/api/free", "", headers);
    EXPECT_EQ(allowed.status, 200);
    EXPECT_EQ(allowed.body, "pong");
    bool metadata = false;
    {
        std::lock_guard lock(backend_mutex);
        for (const auto& header : last_headers) {
            metadata = metadata || header.first == "X-Agent-Wallet";
        }
    }
    EXPECT_TRUE(metadata);

    const auto quote = request(config->server.listen_port, "GET", "/api/paid", "", headers);
    EXPECT_EQ(quote.status, 402);
    EXPECT_NE(quote.body.find("0.001"), std::string::npos);

    auto updated = std::make_shared<gateway::core::Configuration>(*config);
    updated->endpoints[1].price_usdc = 0.25;
    router.applyConfiguration(updated);
    const auto requote = request(config->server.listen_port, "GET", "/api/paid", "", headers);
    EXPECT_EQ(requote.status, 402);
    EXPECT_NE(requote.body.find("0.25"), std::string::npos);

    chain->identity.status = gateway::blockchain::LookupStatus::NotFound;
    chain->identity.nft.reset();
    const auto forbidden = request(config->server.listen_port, "GET", "/api/free", "",
                                   {{"X-Agent-Signature", kSignature},
                                    {"X-Agent-Wallet", "0x4444444444444444444444444444444444444444"}});
    EXPECT_EQ(forbidden.status, 403);

    gateway::blockchain::Transaction payment_tx;
    payment_tx.to_address = chain->wallet;
    payment_tx.from_address = kWallet;
    payment_tx.value_wei = "1000";
    payment_tx.is_finalized = true;
    chain->transaction.status = gateway::blockchain::TxStatus::Found;
    chain->transaction.transaction = payment_tx;
    chain->identity.status = gateway::blockchain::LookupStatus::Found;
    chain->identity.nft = nft;
    auto broken = std::make_shared<gateway::core::Configuration>(*updated);
    broken->endpoints[1].backend_url = "http://127.0.0.1:1";
    broken->endpoints[1].price_usdc = 0.001;
    router.applyConfiguration(broken);
    auto paid_headers = headers;
    paid_headers.emplace_back("X-Payment-Tx", "0xrefund");
    const auto unavailable = request(config->server.listen_port, "POST", "/api/paid", "", paid_headers);
    EXPECT_EQ(unavailable.status, 502);
    const auto refund = request(config->server.listen_port, "POST", "/v1/refunds",
                                R"({"tx_hash":"0xrefund","wallet_address":"0x2222222222222222222222222222222222222222","gas_fee_usdc":0.0001})",
                                headers);
    EXPECT_EQ(refund.status, 200);
    EXPECT_NE(refund.body.find("refunded"), std::string::npos);

    server.requestStop();
    listener.join();
}
