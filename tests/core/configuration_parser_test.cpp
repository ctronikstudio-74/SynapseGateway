#include "gateway/core/ConfigurationManager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using gateway::core::ConfigurationManager;
using gateway::core::Network;
using gateway::core::ProofType;

namespace {

std::filesystem::path writeTemp(const std::string& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::trunc);
    output << contents;
    return path;
}

}  // namespace

TEST(ConfigurationParserTest, LoadsExampleConfiguration) {
    const std::filesystem::path example =
        std::filesystem::path(GATEWAY_SOURCE_DIR) / "config" / "gateway.example.toml";

    const auto loaded = ConfigurationManager::loadFromFile(example);
    ASSERT_TRUE(loaded) << loaded.error();

    const auto& config = loaded.value();
    EXPECT_EQ(config.server.listen_address, "0.0.0.0");
    EXPECT_EQ(config.server.listen_port, 8080);
    EXPECT_EQ(config.server.monitoring_port, 9090);
    EXPECT_EQ(config.server.thread_pool_size, 0u);

    ASSERT_EQ(config.networks.size(), 5u);
    EXPECT_EQ(config.networks[0].network, Network::BASE);
    EXPECT_TRUE(config.networks[0].enabled);
    EXPECT_EQ(config.networks[3].network, Network::ETHEREUM_MAINNET);
    EXPECT_FALSE(config.networks[3].enabled);
    EXPECT_EQ(config.networks[4].network, Network::BNB_CHAIN);

    ASSERT_EQ(config.endpoints.size(), 3u);
    EXPECT_EQ(config.endpoints[0].path_pattern, "/api/v1/chat");
    EXPECT_DOUBLE_EQ(config.endpoints[0].price_usdc, 0.001);
    EXPECT_EQ(config.endpoints[0].required_proofs, std::vector<ProofType>{ProofType::NONE});
    EXPECT_EQ(config.endpoints[1].required_proofs,
              (std::vector<ProofType>{ProofType::ZK_ML, ProofType::STAKING}));
    EXPECT_DOUBLE_EQ(config.endpoints[2].price_usdc, 0.0);

    EXPECT_DOUBLE_EQ(config.default_endpoint.price_usdc, 0.005);
    EXPECT_EQ(config.default_endpoint.rate_limit_per_minute, 50u);
    EXPECT_TRUE(config.default_endpoint.new_agents_allowed);

    EXPECT_EQ(config.cache.max_size_mb, 512u);
    EXPECT_EQ(config.cache.default_ttl.count(), 300);
    EXPECT_EQ(config.security.bloom_filter_size_mb, 100u);
    EXPECT_EQ(config.security.bloom_filter_retention.count(), 720);
    EXPECT_TRUE(config.monitoring.prometheus_enabled);
    EXPECT_FALSE(config.monitoring.opentelemetry_enabled);
    EXPECT_EQ(config.monitoring.log_level, "info");
}

TEST(ConfigurationParserTest, MissingFileReportsPath) {
    const auto loaded = ConfigurationManager::loadFromFile("config/does-not-exist.toml");
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("not found"), std::string::npos);
}

TEST(ConfigurationParserTest, SyntaxErrorIncludesLineNumber) {
    const auto path = writeTemp("agentgw-bad.toml", "[server]\nlisten_address = \"ok\"\nlisten_port = \n");
    const auto loaded = ConfigurationManager::loadFromFile(path);
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("line "), std::string::npos);
    std::filesystem::remove(path);
}

TEST(ConfigurationParserTest, MissingRequiredKeyIncludesLineNumber) {
    const auto path = writeTemp("agentgw-missing.toml", "[server]\nlisten_address = \"127.0.0.1\"\n");
    const auto loaded = ConfigurationManager::loadFromFile(path);
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("line "), std::string::npos);
    EXPECT_NE(loaded.error().find("listen_port"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(ConfigurationParserTest, RejectsUnknownNetworkWithLineNumber) {
    const char* toml = R"(
[server]
listen_address = "127.0.0.1"
listen_port = 8080
monitoring_port = 9090
thread_pool_size = 1

[blockchain]
payment_wallet_seed = "seed"

[[blockchain.networks]]
network = "solana"
rpc_url = "https://example.invalid"
identity_registry = "0x0"
reputation_registry = "0x0"
validation_registry = "0x0"
gas_limit = 1
enabled = true

[[endpoints]]
path_pattern = "/api"
price_usdc = 0
trust_threshold = 0
required_proofs = []
backend_url = "http://localhost"
backend_timeout = 5
rate_limit_per_minute = 1
new_agents_allowed = true

[default_endpoint]
price_usdc = 0
trust_threshold = 0
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
    const auto path = writeTemp("agentgw-network.toml", toml);
    const auto loaded = ConfigurationManager::loadFromFile(path);
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("line "), std::string::npos);
    EXPECT_NE(loaded.error().find("solana"), std::string::npos);
    std::filesystem::remove(path);
}
