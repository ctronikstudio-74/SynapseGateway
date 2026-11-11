#include "gateway/core/Configuration.h"
#include <gtest/gtest.h>

using namespace gateway::core;

/**
 * @brief Test fixture for Configuration data structures
 */
class ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test data
    }

    void TearDown() override {
        // Clean up
    }
};

/**
 * @brief Test Network enum string conversion
 * 
 * Validates: Requirements 3.1, 14.4
 */
TEST_F(ConfigurationTest, NetworkEnumConversion) {
    // Test all supported networks
    EXPECT_EQ(NetworkFromString("ethereum_mainnet"), Network::ETHEREUM_MAINNET);
    EXPECT_EQ(NetworkFromString("ethereum"), Network::ETHEREUM_MAINNET);
    EXPECT_EQ(NetworkFromString("base"), Network::BASE);
    EXPECT_EQ(NetworkFromString("arbitrum"), Network::ARBITRUM);
    EXPECT_EQ(NetworkFromString("polygon"), Network::POLYGON);
    EXPECT_EQ(NetworkFromString("bnb_chain"), Network::BNB_CHAIN);
    EXPECT_EQ(NetworkFromString("bnb"), Network::BNB_CHAIN);

    // Test case insensitivity
    EXPECT_EQ(NetworkFromString("BASE"), Network::BASE);
    EXPECT_EQ(NetworkFromString("Arbitrum"), Network::ARBITRUM);

    // Test reverse conversion
    EXPECT_EQ(NetworkToString(Network::ETHEREUM_MAINNET), "ethereum_mainnet");
    EXPECT_EQ(NetworkToString(Network::BASE), "base");
    EXPECT_EQ(NetworkToString(Network::ARBITRUM), "arbitrum");
    EXPECT_EQ(NetworkToString(Network::POLYGON), "polygon");
    EXPECT_EQ(NetworkToString(Network::BNB_CHAIN), "bnb_chain");

    // Test invalid network name
    EXPECT_THROW(NetworkFromString("invalid_network"), std::invalid_argument);
}

/**
 * @brief Test ProofType enum string conversion
 * 
 * Validates: Requirements 6.1-6.7, 8.4
 */
TEST_F(ConfigurationTest, ProofTypeEnumConversion) {
    // Test all proof types
    EXPECT_EQ(ProofTypeFromString("none"), ProofType::NONE);
    EXPECT_EQ(ProofTypeFromString("zkml"), ProofType::ZK_ML);
    EXPECT_EQ(ProofTypeFromString("zk_ml"), ProofType::ZK_ML);
    EXPECT_EQ(ProofTypeFromString("tee"), ProofType::TEE_ATTESTATION);
    EXPECT_EQ(ProofTypeFromString("tee_attestation"), ProofType::TEE_ATTESTATION);
    EXPECT_EQ(ProofTypeFromString("staking"), ProofType::STAKING);

    // Test case insensitivity
    EXPECT_EQ(ProofTypeFromString("ZKML"), ProofType::ZK_ML);
    EXPECT_EQ(ProofTypeFromString("TEE"), ProofType::TEE_ATTESTATION);

    // Test reverse conversion
    EXPECT_EQ(ProofTypeToString(ProofType::NONE), "none");
    EXPECT_EQ(ProofTypeToString(ProofType::ZK_ML), "zkml");
    EXPECT_EQ(ProofTypeToString(ProofType::TEE_ATTESTATION), "tee_attestation");
    EXPECT_EQ(ProofTypeToString(ProofType::STAKING), "staking");

    // Test invalid proof type
    EXPECT_THROW(ProofTypeFromString("invalid_proof"), std::invalid_argument);
}

/**
 * @brief Test EndpointConfig structure
 * 
 * Validates: Requirements 8.1-8.7
 */
TEST_F(ConfigurationTest, EndpointConfigStructure) {
    EndpointConfig config;
    config.path_pattern = "/api/v1/.*";
    config.price_usdc = 0.001;
    config.trust_threshold = 0.75;
    config.required_proofs = {ProofType::NONE};
    config.backend_url = "http://localhost:3000";
    config.backend_timeout = std::chrono::seconds(5);
    config.rate_limit_per_minute = 100;
    config.new_agents_allowed = true;

    // Verify values
    EXPECT_EQ(config.path_pattern, "/api/v1/.*");
    EXPECT_DOUBLE_EQ(config.price_usdc, 0.001);
    EXPECT_DOUBLE_EQ(config.trust_threshold, 0.75);
    EXPECT_EQ(config.required_proofs.size(), 1);
    EXPECT_EQ(config.required_proofs[0], ProofType::NONE);
    EXPECT_EQ(config.backend_url, "http://localhost:3000");
    EXPECT_EQ(config.backend_timeout, std::chrono::seconds(5));
    EXPECT_EQ(config.rate_limit_per_minute, 100);
    EXPECT_TRUE(config.new_agents_allowed);

    // Test equality operator
    EndpointConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test BlockchainNetworkConfig structure
 * 
 * Validates: Requirements 3.1, 3.3, 14.4
 */
TEST_F(ConfigurationTest, BlockchainNetworkConfigStructure) {
    BlockchainNetworkConfig config;
    config.network = Network::BASE;
    config.rpc_url = "https://mainnet.base.org";
    config.identity_registry = "0x1234567890abcdef1234567890abcdef12345678";
    config.reputation_registry = "0xabcdef1234567890abcdef1234567890abcdef12";
    config.validation_registry = "0x9876543210fedcba9876543210fedcba98765432";
    config.gas_limit = 500000;
    config.enabled = true;

    // Verify values
    EXPECT_EQ(config.network, Network::BASE);
    EXPECT_EQ(config.rpc_url, "https://mainnet.base.org");
    EXPECT_EQ(config.identity_registry, "0x1234567890abcdef1234567890abcdef12345678");
    EXPECT_EQ(config.reputation_registry, "0xabcdef1234567890abcdef1234567890abcdef12");
    EXPECT_EQ(config.validation_registry, "0x9876543210fedcba9876543210fedcba98765432");
    EXPECT_EQ(config.gas_limit, 500000);
    EXPECT_TRUE(config.enabled);

    // Test equality operator
    BlockchainNetworkConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test ServerConfig structure
 * 
 * Validates: Requirements 8.1
 */
TEST_F(ConfigurationTest, ServerConfigStructure) {
    ServerConfig config;
    config.listen_address = "0.0.0.0";
    config.listen_port = 8080;
    config.monitoring_port = 9090;
    config.thread_pool_size = 8;

    // Verify values
    EXPECT_EQ(config.listen_address, "0.0.0.0");
    EXPECT_EQ(config.listen_port, 8080);
    EXPECT_EQ(config.monitoring_port, 9090);
    EXPECT_EQ(config.thread_pool_size, 8);

    // Test equality operator
    ServerConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test CacheConfig structure
 * 
 * Validates: Requirements 2.8, 5.8, 10.2
 */
TEST_F(ConfigurationTest, CacheConfigStructure) {
    CacheConfig config;
    config.max_size_mb = 512;
    config.default_ttl = std::chrono::seconds(300);

    // Verify values
    EXPECT_EQ(config.max_size_mb, 512);
    EXPECT_EQ(config.default_ttl, std::chrono::seconds(300));

    // Test equality operator
    CacheConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test SecurityConfig structure
 * 
 * Validates: Requirements 11.1, 11.2
 */
TEST_F(ConfigurationTest, SecurityConfigStructure) {
    SecurityConfig config;
    config.bloom_filter_size_mb = 100;
    config.bloom_filter_retention = std::chrono::hours(720);

    // Verify values
    EXPECT_EQ(config.bloom_filter_size_mb, 100);
    EXPECT_EQ(config.bloom_filter_retention, std::chrono::hours(720));

    // Test equality operator
    SecurityConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test MonitoringConfig structure
 * 
 * Validates: Requirements 12.1-12.7
 */
TEST_F(ConfigurationTest, MonitoringConfigStructure) {
    MonitoringConfig config;
    config.prometheus_enabled = true;
    config.opentelemetry_enabled = false;
    config.log_level = "info";

    // Verify values
    EXPECT_TRUE(config.prometheus_enabled);
    EXPECT_FALSE(config.opentelemetry_enabled);
    EXPECT_EQ(config.log_level, "info");

    // Test equality operator
    MonitoringConfig config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test complete Configuration structure
 * 
 * Validates: Requirements 8.1-8.8, 14.1-14.5
 */
TEST_F(ConfigurationTest, CompleteConfigurationStructure) {
    Configuration config;

    // Set up server config
    config.server.listen_address = "0.0.0.0";
    config.server.listen_port = 8080;
    config.server.monitoring_port = 9090;
    config.server.thread_pool_size = 8;

    // Set up blockchain config
    config.payment_wallet_seed = "test-seed-for-deterministic-wallets";
    
    BlockchainNetworkConfig network_config;
    network_config.network = Network::BASE;
    network_config.rpc_url = "https://mainnet.base.org";
    network_config.identity_registry = "0x1234567890abcdef1234567890abcdef12345678";
    network_config.reputation_registry = "0xabcdef1234567890abcdef1234567890abcdef12";
    network_config.validation_registry = "0x9876543210fedcba9876543210fedcba98765432";
    network_config.gas_limit = 500000;
    network_config.enabled = true;
    config.networks.push_back(network_config);

    // Set up endpoint config
    EndpointConfig endpoint_config;
    endpoint_config.path_pattern = "/api/v1/.*";
    endpoint_config.price_usdc = 0.001;
    endpoint_config.trust_threshold = 0.75;
    endpoint_config.required_proofs = {ProofType::NONE};
    endpoint_config.backend_url = "http://localhost:3000";
    endpoint_config.backend_timeout = std::chrono::seconds(5);
    endpoint_config.rate_limit_per_minute = 100;
    endpoint_config.new_agents_allowed = true;
    config.endpoints.push_back(endpoint_config);

    // Set up default endpoint
    config.default_endpoint = endpoint_config;

    // Set up cache config
    config.cache.max_size_mb = 512;
    config.cache.default_ttl = std::chrono::seconds(300);

    // Set up security config
    config.security.bloom_filter_size_mb = 100;
    config.security.bloom_filter_retention = std::chrono::hours(720);

    // Set up monitoring config
    config.monitoring.prometheus_enabled = true;
    config.monitoring.opentelemetry_enabled = false;
    config.monitoring.log_level = "info";

    // Verify structure integrity
    EXPECT_EQ(config.server.listen_port, 8080);
    EXPECT_EQ(config.networks.size(), 1);
    EXPECT_EQ(config.networks[0].network, Network::BASE);
    EXPECT_EQ(config.endpoints.size(), 1);
    EXPECT_EQ(config.endpoints[0].path_pattern, "/api/v1/.*");
    EXPECT_EQ(config.cache.max_size_mb, 512);
    EXPECT_EQ(config.security.bloom_filter_size_mb, 100);
    EXPECT_TRUE(config.monitoring.prometheus_enabled);

    // Test equality operator (Requirement 14.7)
    Configuration config2 = config;
    EXPECT_EQ(config, config2);
}

/**
 * @brief Test multiple proof types configuration
 * 
 * Validates: Requirements 8.4
 */
TEST_F(ConfigurationTest, MultipleProofTypesConfiguration) {
    EndpointConfig config;
    config.path_pattern = "/api/v1/premium/.*";
    config.price_usdc = 0.01;
    config.trust_threshold = 0.85;
    // Require multiple proof types
    config.required_proofs = {ProofType::ZK_ML, ProofType::STAKING};
    config.backend_url = "http://localhost:3000/premium";
    config.backend_timeout = std::chrono::seconds(10);
    config.rate_limit_per_minute = 20;
    config.new_agents_allowed = false;

    // Verify multiple proof types
    EXPECT_EQ(config.required_proofs.size(), 2);
    EXPECT_EQ(config.required_proofs[0], ProofType::ZK_ML);
    EXPECT_EQ(config.required_proofs[1], ProofType::STAKING);
    EXPECT_FALSE(config.new_agents_allowed);
}

/**
 * @brief Test multiple blockchain networks configuration
 * 
 * Validates: Requirements 3.1
 */
TEST_F(ConfigurationTest, MultipleNetworksConfiguration) {
    Configuration config;

    // Add multiple networks
    BlockchainNetworkConfig base_config;
    base_config.network = Network::BASE;
    base_config.rpc_url = "https://mainnet.base.org";
    base_config.enabled = true;
    config.networks.push_back(base_config);

    BlockchainNetworkConfig arbitrum_config;
    arbitrum_config.network = Network::ARBITRUM;
    arbitrum_config.rpc_url = "https://arb1.arbitrum.io/rpc";
    arbitrum_config.enabled = true;
    config.networks.push_back(arbitrum_config);

    BlockchainNetworkConfig polygon_config;
    polygon_config.network = Network::POLYGON;
    polygon_config.rpc_url = "https://polygon-rpc.com";
    polygon_config.enabled = false;  // Disabled network
    config.networks.push_back(polygon_config);

    // Verify multiple networks
    EXPECT_EQ(config.networks.size(), 3);
    EXPECT_EQ(config.networks[0].network, Network::BASE);
    EXPECT_EQ(config.networks[1].network, Network::ARBITRUM);
    EXPECT_EQ(config.networks[2].network, Network::POLYGON);
    EXPECT_TRUE(config.networks[0].enabled);
    EXPECT_TRUE(config.networks[1].enabled);
    EXPECT_FALSE(config.networks[2].enabled);
}

/**
 * @brief Test zero-price endpoint configuration
 * 
 * Validates: Requirements 2.3 (zero-price fast path)
 */
TEST_F(ConfigurationTest, ZeroPriceEndpointConfiguration) {
    EndpointConfig config;
    config.path_pattern = "/api/v1/free/.*";
    config.price_usdc = 0.0;  // Zero price - no payment required
    config.trust_threshold = 0.5;
    config.required_proofs = {ProofType::NONE};
    config.backend_url = "http://localhost:3000/free";
    config.backend_timeout = std::chrono::seconds(5);
    config.rate_limit_per_minute = 1000;
    config.new_agents_allowed = true;

    // Verify zero price configuration
    EXPECT_DOUBLE_EQ(config.price_usdc, 0.0);
    EXPECT_EQ(config.required_proofs[0], ProofType::NONE);
}
