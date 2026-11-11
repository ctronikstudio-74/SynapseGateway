#include "gateway/core/ConfigurationManager.h"

#include "../support/property.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>

using namespace gateway::core;

namespace {

std::string address(std::uint64_t seed) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(40) << (seed & 0xffffffffffULL);
    return out.str();
}

bool near(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

void expectEquivalent(const Configuration& left, const Configuration& right) {
    EXPECT_EQ(left.server, right.server);
    EXPECT_EQ(left.payment_wallet_seed, right.payment_wallet_seed);
    ASSERT_EQ(left.networks.size(), right.networks.size());
    for (std::size_t i = 0; i < left.networks.size(); ++i) {
        EXPECT_EQ(left.networks[i], right.networks[i]);
    }
    ASSERT_EQ(left.endpoints.size(), right.endpoints.size());
    for (std::size_t i = 0; i < left.endpoints.size(); ++i) {
        EXPECT_EQ(left.endpoints[i].path_pattern, right.endpoints[i].path_pattern);
        EXPECT_TRUE(near(left.endpoints[i].price_usdc, right.endpoints[i].price_usdc));
        EXPECT_TRUE(near(left.endpoints[i].trust_threshold, right.endpoints[i].trust_threshold));
        EXPECT_EQ(left.endpoints[i].required_proofs, right.endpoints[i].required_proofs);
        EXPECT_EQ(left.endpoints[i].backend_url, right.endpoints[i].backend_url);
        EXPECT_EQ(left.endpoints[i].backend_timeout, right.endpoints[i].backend_timeout);
        EXPECT_EQ(left.endpoints[i].rate_limit_per_minute, right.endpoints[i].rate_limit_per_minute);
        EXPECT_EQ(left.endpoints[i].new_agents_allowed, right.endpoints[i].new_agents_allowed);
    }
    EXPECT_TRUE(near(left.default_endpoint.price_usdc, right.default_endpoint.price_usdc));
    EXPECT_TRUE(near(left.default_endpoint.trust_threshold, right.default_endpoint.trust_threshold));
    EXPECT_EQ(left.default_endpoint.backend_url, right.default_endpoint.backend_url);
    EXPECT_EQ(left.cache, right.cache);
    EXPECT_EQ(left.security, right.security);
    EXPECT_EQ(left.monitoring, right.monitoring);
}

Configuration sample(std::mt19937& rng) {
    std::uniform_int_distribution<int> small(1, 5);
    std::uniform_int_distribution<int> price_millis(0, 1000);
    std::uniform_int_distribution<int> trust_points(0, 100);
    std::uniform_int_distribution<int> coin(0, 1);
    Configuration config;
    config.server.listen_address = "127.0.0.1";
    config.server.listen_port = 8080;
    config.server.monitoring_port = 9090;
    config.server.thread_pool_size = static_cast<std::uint32_t>(small(rng));
    config.payment_wallet_seed = "seed-" + std::to_string(small(rng));
    const Network networks[] = {Network::BASE, Network::ARBITRUM, Network::POLYGON};
    const int network_count = 1 + (small(rng) % 3);
    for (int i = 0; i < network_count; ++i) {
        BlockchainNetworkConfig network;
        network.network = networks[i];
        network.rpc_url = "https://rpc.example/" + NetworkToString(network.network);
        network.identity_registry = address(0x1000ULL + static_cast<unsigned>(i));
        network.reputation_registry = address(0x2000ULL + static_cast<unsigned>(i));
        network.validation_registry = address(0x3000ULL + static_cast<unsigned>(i));
        network.gas_limit = 100000;
        network.enabled = coin(rng) == 1;
        config.networks.push_back(network);
    }
    EndpointConfig endpoint;
    endpoint.path_pattern = "/api/" + std::to_string(small(rng));
    endpoint.price_usdc = static_cast<double>(price_millis(rng)) / 1000.0;
    endpoint.trust_threshold = static_cast<double>(trust_points(rng)) / 100.0;
    endpoint.required_proofs = {coin(rng) == 0 ? ProofType::NONE : ProofType::STAKING};
    endpoint.backend_url = "https://backend.example/service";
    endpoint.backend_timeout = std::chrono::seconds{small(rng)};
    endpoint.rate_limit_per_minute = static_cast<std::uint32_t>(small(rng) * 10);
    endpoint.new_agents_allowed = coin(rng) == 1;
    config.endpoints.push_back(endpoint);
    config.default_endpoint = endpoint;
    config.default_endpoint.path_pattern.clear();
    config.cache.max_size_mb = static_cast<std::size_t>(small(rng));
    config.cache.default_ttl = std::chrono::seconds{30 * small(rng)};
    config.security.bloom_filter_size_mb = static_cast<std::uint32_t>(small(rng));
    config.security.bloom_filter_retention = std::chrono::hours{24 * small(rng)};
    config.monitoring.prometheus_enabled = coin(rng) == 1;
    config.monitoring.opentelemetry_enabled = false;
    config.monitoring.log_level = "info";
    return config;
}

}  // namespace

TEST(ConfigurationRoundTrip, ExampleFileValidates) {
    const auto loaded =
        ConfigurationManager::loadFromFile(std::filesystem::path(GATEWAY_SOURCE_DIR) / "config" / "gateway.example.toml");
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_TRUE(ConfigurationManager::validate(loaded.value()).empty());
}

TEST(ConfigurationRoundTrip, PropertyParseSerializeParse) {
    gateway::test::checkProperty("config round trip", [](std::mt19937& rng) {
        const Configuration original = sample(rng);
        ASSERT_TRUE(ConfigurationManager::validate(original).empty());
        const auto parsed = ConfigurationManager::loadFromString(ConfigurationManager::serialize(original));
        ASSERT_TRUE(parsed) << parsed.error();
        expectEquivalent(original, parsed.value());
    });
}

TEST(ConfigurationValidation, InvalidTrustThresholdNamesTheFieldAndLine) {
    const char* toml = R"(
[server]
listen_address = "127.0.0.1"
listen_port = 8080
monitoring_port = 9090
thread_pool_size = 1

[blockchain]
payment_wallet_seed = "seed"

[[blockchain.networks]]
network = "base"
rpc_url = "https://mainnet.base.org"
identity_registry = "0x0000000000000000000000000000000000000001"
reputation_registry = "0x0000000000000000000000000000000000000001"
validation_registry = "0x0000000000000000000000000000000000000001"
gas_limit = 10
enabled = true

[[endpoints]]
path_pattern = "/api"
price_usdc = 1
trust_threshold = 1.5
required_proofs = []
backend_url = "http://localhost"
backend_timeout = 5
rate_limit_per_minute = 1
new_agents_allowed = true

[default_endpoint]
price_usdc = 1
trust_threshold = 0.5
required_proofs = []
backend_url = "http://localhost"
backend_timeout = 5
rate_limit_per_minute = 1
new_agents_allowed = true

[cache]
max_size_mb = 1
default_ttl_seconds = 1

[security]
bloom_filter_size_mb = 1
bloom_filter_retention_hours = 1

[monitoring]
prometheus_enabled = false
opentelemetry_enabled = false
log_level = "info"
)";
    const auto loaded = ConfigurationManager::loadFromString(toml);
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("trust_threshold"), std::string::npos);
    EXPECT_NE(loaded.error().find("line "), std::string::npos);
}

TEST(ConfigurationValidation, InMemoryErrorNamesTheField) {
    const auto loaded =
        ConfigurationManager::loadFromFile(std::filesystem::path(GATEWAY_SOURCE_DIR) / "config" / "gateway.example.toml");
    ASSERT_TRUE(loaded);
    Configuration config = loaded.value();
    config.endpoints[0].trust_threshold = 2.0;
    const auto errors = ConfigurationManager::validate(config);
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors.front().find("trust_threshold"), std::string::npos);
    EXPECT_NE(errors.front().find("endpoints[0]"), std::string::npos);
}

TEST(ConfigurationHotReload, AppliesFileChangeWithinThePollWindow) {
    const auto loaded =
        ConfigurationManager::loadFromFile(std::filesystem::path(GATEWAY_SOURCE_DIR) / "config" / "gateway.example.toml");
    ASSERT_TRUE(loaded);
    Configuration config = loaded.value();
    config.endpoints[0].price_usdc = 0.25;
    const auto path = std::filesystem::temp_directory_path() / "agentgw-reload.toml";
    {
        std::ofstream output(path, std::ios::trunc);
        output << ConfigurationManager::serialize(config);
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::vector<double> prices;
    auto watcher = ConfigurationManager::watchForChanges(
        path,
        [&](Configuration updated) {
            std::lock_guard lock(mutex);
            if (!updated.endpoints.empty()) {
                prices.push_back(updated.endpoints[0].price_usdc);
            }
            ready.notify_all();
        },
        std::chrono::milliseconds{50});

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds{3}, [&] { return !prices.empty(); }));
    }

    config.endpoints[0].price_usdc = 0.5;
    config.endpoints[0].path_pattern = "/api/v1/chat/reloaded";
    {
        std::ofstream output(path, std::ios::trunc);
        output << ConfigurationManager::serialize(config);
    }
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds{3}, [&] {
            return std::any_of(prices.begin(), prices.end(), [](double price) { return std::abs(price - 0.5) < 1e-9; });
        }));
    }
    watcher.reset();
    std::filesystem::remove(path);
}
