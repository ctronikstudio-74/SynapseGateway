#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace gateway::core {

/**
 * @brief Blockchain network identifiers for multi-chain support
 * 
 * Represents supported blockchain networks for payment processing and
 * smart contract interaction.
 */
enum class Network {
    ETHEREUM_MAINNET,
    BASE,
    ARBITRUM,
    POLYGON,
    BNB_CHAIN
};

/**
 * @brief Proof-of-work validation types for agent authorization
 * 
 * Different types of cryptographic proofs that can be required for
 * endpoint access.
 */
enum class ProofType {
    NONE,           // No proof required
    ZK_ML,          // Zero-knowledge machine learning proof
    TEE_ATTESTATION, // Trusted Execution Environment attestation
    STAKING         // Minimum stake requirement
};

/**
 * @brief Configuration for individual API endpoints
 * 
 * Defines payment pricing, trust requirements, backend routing, and
 * rate limiting for each protected endpoint.
 * 
 * Requirements: 8.1-8.7
 */
struct EndpointConfig {
    std::string path_pattern;           ///< Regex pattern for endpoint matching (e.g., "/api/v1/.*")
    double price_usdc;                  ///< Price per request in USDC (decimal precision)
    double trust_threshold;             ///< Minimum reputation score (0.0 - 1.0) for authorization
    std::vector<ProofType> required_proofs; ///< Required proof-of-work types (combinations allowed)
    std::string backend_url;            ///< Target backend API URL
    std::chrono::seconds backend_timeout{5}; ///< Backend connection timeout
    uint32_t rate_limit_per_minute;     ///< Maximum requests per minute per wallet address
    bool new_agents_allowed;            ///< Allow agents without reputation data

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const EndpointConfig& other) const = default;
};

/**
 * @brief Blockchain network connection configuration
 * 
 * Defines RPC endpoints and smart contract addresses for each
 * supported blockchain network.
 * 
 * Requirements: 3.1, 3.3, 14.4
 */
struct BlockchainNetworkConfig {
    Network network;                    ///< Network identifier
    std::string rpc_url;                ///< JSON-RPC endpoint URL (HTTP or WebSocket)
    std::string identity_registry;      ///< ERC-721 identity registry contract address
    std::string reputation_registry;    ///< Reputation registry contract address
    std::string validation_registry;    ///< Validation registry contract address (zkML, TEE, staking)
    uint64_t gas_limit;                 ///< Maximum gas limit for transactions
    bool enabled;                       ///< Network availability flag

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const BlockchainNetworkConfig& other) const = default;
};

/**
 * @brief Server configuration settings
 */
struct ServerConfig {
    std::string listen_address;         ///< HTTP server bind address (e.g., "0.0.0.0")
    uint16_t listen_port;               ///< HTTP server port (default: 8080)
    uint16_t monitoring_port;           ///< Prometheus metrics port (default: 9090)
    uint32_t thread_pool_size;          ///< Number of worker threads for request handling

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const ServerConfig& other) const = default;
};

/**
 * @brief Cache configuration settings
 */
struct CacheConfig {
    size_t max_size_mb;                 ///< Maximum cache size in megabytes
    std::chrono::seconds default_ttl{300}; ///< Default time-to-live for cached entries

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const CacheConfig& other) const = default;
};

/**
 * @brief Security configuration settings
 */
struct SecurityConfig {
    uint32_t bloom_filter_size_mb;      ///< Bloom filter size for replay attack prevention
    std::chrono::hours bloom_filter_retention{720}; ///< Transaction hash retention period (30 days)

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const SecurityConfig& other) const = default;
};

/**
 * @brief Monitoring and observability configuration
 */
struct MonitoringConfig {
    bool prometheus_enabled;            ///< Enable Prometheus metrics exposition
    bool opentelemetry_enabled;         ///< Enable OpenTelemetry distributed tracing
    std::string log_level;              ///< Logging level: "trace", "debug", "info", "warn", "error"

    /**
     * @brief Equality comparison for configuration validation
     */
    bool operator==(const MonitoringConfig& other) const = default;
};

/**
 * @brief Complete AgentGateway configuration
 * 
 * Aggregates all configuration sections for the gateway system.
 * Supports loading from TOML files and hot-reload during runtime.
 * 
 * Requirements: 8.1-8.8, 14.1-14.5
 */
struct Configuration {
    // Server configuration
    ServerConfig server;

    // Blockchain configuration
    std::vector<BlockchainNetworkConfig> networks;
    std::string payment_wallet_seed;    ///< Seed for deterministic wallet generation (CREATE2)

    // Endpoint configuration
    std::vector<EndpointConfig> endpoints;
    EndpointConfig default_endpoint;    ///< Fallback configuration for undefined endpoints

    // Cache configuration
    CacheConfig cache;

    // Security configuration
    SecurityConfig security;

    // Monitoring configuration
    MonitoringConfig monitoring;

    /**
     * @brief Equality comparison for round-trip testing (Requirement 14.7)
     */
    bool operator==(const Configuration& other) const = default;
};

// Utility functions for enum conversion

/**
 * @brief Convert string to Network enum
 * @param str Network name string (case-insensitive)
 * @return Network enum value
 * @throws std::invalid_argument if string is not a valid network name
 */
Network NetworkFromString(const std::string& str);

/**
 * @brief Convert Network enum to string
 * @param network Network enum value
 * @return Network name string
 */
std::string NetworkToString(Network network);

/**
 * @brief Convert string to ProofType enum
 * @param str Proof type string (case-insensitive)
 * @return ProofType enum value
 * @throws std::invalid_argument if string is not a valid proof type
 */
ProofType ProofTypeFromString(const std::string& str);

/**
 * @brief Convert ProofType enum to string
 * @param proof_type ProofType enum value
 * @return Proof type string
 */
std::string ProofTypeToString(ProofType proof_type);

} // namespace gateway::core
