#include "gateway/core/ConfigurationManager.h"

#include "gateway/core/validation_rules.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace gateway::core {
namespace {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::optional<std::string> environmentValue(const std::string& name) {
#if defined(_MSC_VER)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name.c_str()) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::string at(const toml::node& node, std::string_view message) {
    return "line " + std::to_string(node.source().begin.line) + ": " + std::string(message);
}

const toml::node& requireKey(const toml::table& table, std::string_view key) {
    if (const toml::node* node = table.get(key)) {
        return *node;
    }
    throw ParseError(at(table, "missing required key '" + std::string(key) + "'"));
}

std::string readString(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    if (auto value = node.value<std::string>()) {
        return *value;
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be a string"));
}

int64_t readInt(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    if (auto value = node.value<int64_t>()) {
        return *value;
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be an integer"));
}

bool readBool(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    if (auto value = node.value<bool>()) {
        return *value;
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be a boolean"));
}

double readDouble(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    if (auto value = node.value<double>()) {
        return *value;
    }
    if (auto value = node.value<int64_t>()) {
        return static_cast<double>(*value);
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be a number"));
}

uint16_t readPort(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    const auto value = readInt(table, key);
    if (value < 0 || value > 65535) {
        throw ParseError(at(node, "key '" + std::string(key) + "' must be a port in [0, 65535]"));
    }
    return static_cast<uint16_t>(value);
}

uint32_t readUint32(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    const auto value = readInt(table, key);
    if (value < 0) {
        throw ParseError(at(node, "key '" + std::string(key) + "' must be >= 0"));
    }
    return static_cast<uint32_t>(value);
}

uint64_t readUint64(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    const auto value = readInt(table, key);
    if (value < 0) {
        throw ParseError(at(node, "key '" + std::string(key) + "' must be >= 0"));
    }
    return static_cast<uint64_t>(value);
}

std::chrono::seconds readSeconds(const toml::table& table, std::string_view key) {
    const toml::node& node = requireKey(table, key);
    const auto value = readInt(table, key);
    if (value < 0) {
        throw ParseError(at(node, "key '" + std::string(key) + "' must be >= 0"));
    }
    return std::chrono::seconds{value};
}

const toml::table& requireTable(const toml::table& root, std::string_view key) {
    const toml::node& node = requireKey(root, key);
    if (const toml::table* table = node.as_table()) {
        return *table;
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be a table"));
}

const toml::array& requireArray(const toml::table& root, std::string_view key) {
    const toml::node& node = requireKey(root, key);
    if (const toml::array* array = node.as_array()) {
        return *array;
    }
    throw ParseError(at(node, "key '" + std::string(key) + "' must be an array"));
}

Network readNetwork(const toml::table& table) {
    const toml::node& node = requireKey(table, "network");
    if (auto name = node.value<std::string>()) {
        try {
            return NetworkFromString(*name);
        } catch (const std::invalid_argument& ex) {
            throw ParseError(at(node, ex.what()));
        }
    }
    throw ParseError(at(node, "key 'network' must be a string"));
}

std::vector<ProofType> readProofs(const toml::table& table) {
    const toml::node& node = requireKey(table, "required_proofs");
    const toml::array* array = node.as_array();
    if (array == nullptr) {
        throw ParseError(at(node, "key 'required_proofs' must be an array of strings"));
    }

    std::vector<ProofType> proofs;
    proofs.reserve(array->size());
    for (const toml::node& item : *array) {
        auto name = item.value<std::string>();
        if (!name) {
            throw ParseError(at(item, "proof type must be a string"));
        }
        try {
            proofs.push_back(ProofTypeFromString(*name));
        } catch (const std::invalid_argument& ex) {
            throw ParseError(at(item, ex.what()));
        }
    }
    return proofs;
}

EndpointConfig readEndpoint(const toml::table& table, bool require_path) {
    EndpointConfig endpoint;
    if (require_path) {
        endpoint.path_pattern = readString(table, "path_pattern");
    } else if (const toml::node* path = table.get("path_pattern")) {
        if (auto value = path->value<std::string>()) {
            endpoint.path_pattern = *value;
        } else {
            throw ParseError(at(*path, "key 'path_pattern' must be a string"));
        }
    }
    endpoint.price_usdc = readDouble(table, "price_usdc");
    endpoint.trust_threshold = readDouble(table, "trust_threshold");
    endpoint.required_proofs = readProofs(table);
    endpoint.backend_url = readString(table, "backend_url");
    endpoint.backend_timeout = readSeconds(table, "backend_timeout");
    endpoint.rate_limit_per_minute = readUint32(table, "rate_limit_per_minute");
    endpoint.new_agents_allowed = readBool(table, "new_agents_allowed");
    if (require_path && endpoint.path_pattern.empty()) {
        throw ParseError(at(requireKey(table, "path_pattern"), "key 'path_pattern' must be non-empty"));
    }
    if (endpoint.price_usdc < 0.0) {
        throw ParseError(at(requireKey(table, "price_usdc"), "key 'price_usdc' must be >= 0"));
    }
    if (endpoint.trust_threshold < 0.0 || endpoint.trust_threshold > 1.0) {
        throw ParseError(at(requireKey(table, "trust_threshold"), "key 'trust_threshold' must be in [0, 1]"));
    }
    if (!isValidHttpUrl(endpoint.backend_url)) {
        throw ParseError(at(requireKey(table, "backend_url"), "key 'backend_url' must be an http or https URL"));
    }
    if (endpoint.backend_timeout.count() <= 0) {
        throw ParseError(at(requireKey(table, "backend_timeout"), "key 'backend_timeout' must be >= 1"));
    }
    return endpoint;
}

ServerConfig readServer(const toml::table& root) {
    const toml::table& table = requireTable(root, "server");
    ServerConfig server;
    server.listen_address = readString(table, "listen_address");
    server.listen_port = readPort(table, "listen_port");
    server.monitoring_port = readPort(table, "monitoring_port");
    server.thread_pool_size = readUint32(table, "thread_pool_size");
    return server;
}

BlockchainNetworkConfig readNetworkConfig(const toml::table& table) {
    BlockchainNetworkConfig network;
    network.network = readNetwork(table);
    network.rpc_url = readString(table, "rpc_url");
    network.identity_registry = readString(table, "identity_registry");
    network.reputation_registry = readString(table, "reputation_registry");
    network.validation_registry = readString(table, "validation_registry");
    network.gas_limit = readUint64(table, "gas_limit");
    network.enabled = readBool(table, "enabled");
    if (!isValidRpcUrl(network.rpc_url)) {
        throw ParseError(at(requireKey(table, "rpc_url"), "key 'rpc_url' must be an http, https, ws, or wss URL"));
    }
    if (!isValidAddress(network.identity_registry)) {
        throw ParseError(at(requireKey(table, "identity_registry"),
                            "key 'identity_registry' must be a 0x-prefixed 20-byte address"));
    }
    if (!isValidAddress(network.reputation_registry)) {
        throw ParseError(at(requireKey(table, "reputation_registry"),
                            "key 'reputation_registry' must be a 0x-prefixed 20-byte address"));
    }
    if (!isValidAddress(network.validation_registry)) {
        throw ParseError(at(requireKey(table, "validation_registry"),
                            "key 'validation_registry' must be a 0x-prefixed 20-byte address"));
    }
    if (network.gas_limit == 0) {
        throw ParseError(at(requireKey(table, "gas_limit"), "key 'gas_limit' must be > 0"));
    }
    return network;
}

void readBlockchain(const toml::table& root, Configuration& config) {
    const toml::table& table = requireTable(root, "blockchain");
    config.payment_wallet_seed = readString(table, "payment_wallet_seed");
    if (config.payment_wallet_seed.empty()) {
        throw ParseError(at(requireKey(table, "payment_wallet_seed"), "key 'payment_wallet_seed' must be non-empty"));
    }
    const toml::array& networks = requireArray(table, "networks");
    config.networks.reserve(networks.size());
    std::set<Network> seen_networks;
    for (const toml::node& item : networks) {
        const toml::table* row = item.as_table();
        if (row == nullptr) {
            throw ParseError(at(item, "blockchain.networks entries must be tables"));
        }
        const BlockchainNetworkConfig network = readNetworkConfig(*row);
        if (!seen_networks.insert(network.network).second) {
            throw ParseError(at(*row, "duplicate blockchain network '" + NetworkToString(network.network) + "'"));
        }
        config.networks.push_back(network);
    }
}

void readEndpoints(const toml::table& root, Configuration& config) {
    const toml::array& endpoints = requireArray(root, "endpoints");
    config.endpoints.reserve(endpoints.size());
    for (const toml::node& item : endpoints) {
        const toml::table* row = item.as_table();
        if (row == nullptr) {
            throw ParseError(at(item, "endpoints entries must be tables"));
        }
        config.endpoints.push_back(readEndpoint(*row, true));
    }
    config.default_endpoint = readEndpoint(requireTable(root, "default_endpoint"), false);
}

CacheConfig readCache(const toml::table& root) {
    const toml::table& table = requireTable(root, "cache");
    CacheConfig cache;
    cache.max_size_mb = static_cast<size_t>(readUint64(table, "max_size_mb"));
    cache.default_ttl = readSeconds(table, "default_ttl_seconds");
    return cache;
}

SecurityConfig readSecurity(const toml::table& root) {
    const toml::table& table = requireTable(root, "security");
    SecurityConfig security;
    security.bloom_filter_size_mb = readUint32(table, "bloom_filter_size_mb");
    const auto hours = readInt(table, "bloom_filter_retention_hours");
    if (hours < 0) {
        throw ParseError(at(requireKey(table, "bloom_filter_retention_hours"),
                            "key 'bloom_filter_retention_hours' must be >= 0"));
    }
    security.bloom_filter_retention = std::chrono::hours{hours};
    return security;
}

MonitoringConfig readMonitoring(const toml::table& root) {
    const toml::table& table = requireTable(root, "monitoring");
    MonitoringConfig monitoring;
    monitoring.prometheus_enabled = readBool(table, "prometheus_enabled");
    monitoring.opentelemetry_enabled = readBool(table, "opentelemetry_enabled");
    monitoring.log_level = readString(table, "log_level");
    if (!isValidLogLevel(monitoring.log_level)) {
        throw ParseError(at(requireKey(table, "log_level"),
                            "key 'log_level' must be trace, debug, info, warn, or error"));
    }
    return monitoring;
}

std::string formatTomlError(const toml::parse_error& error) {
    const auto line = error.source().begin.line;
    return "line " + std::to_string(line) + ": " + std::string(error.description());
}

Configuration buildConfiguration(const toml::table& root) {
    Configuration config;
    config.server = readServer(root);
    readBlockchain(root, config);
    readEndpoints(root, config);
    config.cache = readCache(root);
    config.security = readSecurity(root);
    config.monitoring = readMonitoring(root);
    return config;
}

}  // namespace

std::string ConfigurationManager::expandEnvironmentPlaceholders(std::string_view text) {
    std::string expanded;
    expanded.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '$' && index + 1 < text.size() && text[index + 1] == '{') {
            const auto end = text.find('}', index + 2);
            if (end == std::string_view::npos) {
                expanded.push_back(text[index]);
                ++index;
                continue;
            }
            const std::string name(text.substr(index + 2, end - (index + 2)));
            if (const auto value = environmentValue(name)) {
                expanded += *value;
            } else {
                expanded.append(text.substr(index, end - index + 1));
            }
            index = end + 1;
            continue;
        }
        expanded.push_back(text[index]);
        ++index;
    }
    return expanded;
}

Expected<Configuration> ConfigurationManager::loadFromFile(const std::filesystem::path& config_path) {
    std::error_code exists_error;
    if (!std::filesystem::exists(config_path, exists_error) || exists_error) {
        return Expected<Configuration>::failure("configuration file not found: " + config_path.string());
    }

    std::ifstream input(config_path);
    if (!input) {
        return Expected<Configuration>::failure("configuration file not found: " + config_path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return loadFromString(expandEnvironmentPlaceholders(buffer.str()));
}

Expected<Configuration> ConfigurationManager::loadFromString(std::string_view toml_text) {
    try {
#if TOML_EXCEPTIONS
        const toml::table root = toml::parse(toml_text);
        return Expected<Configuration>::success(buildConfiguration(root));
#else
        toml::parse_result parsed = toml::parse(toml_text);
        if (parsed.failed()) {
            return Expected<Configuration>::failure(formatTomlError(parsed.error()));
        }
        return Expected<Configuration>::success(buildConfiguration(parsed.table()));
#endif
    } catch (const toml::parse_error& error) {
        return Expected<Configuration>::failure(formatTomlError(error));
    } catch (const ParseError& error) {
        return Expected<Configuration>::failure(error.what());
    }
}

std::vector<std::string> ConfigurationManager::validate(const Configuration& config) {
    std::vector<std::string> errors;
    const auto add = [&errors](std::string message) { errors.push_back(std::move(message)); };
    if (config.server.listen_address.empty()) {
        add("server.listen_address is required");
    }
    if (config.server.listen_port == 0) {
        add("server.listen_port must be in [1, 65535]");
    }
    if (config.server.monitoring_port == 0) {
        add("server.monitoring_port must be in [1, 65535]");
    }
    if (config.payment_wallet_seed.empty()) {
        add("blockchain.payment_wallet_seed is required");
    }
    if (config.networks.empty()) {
        add("blockchain.networks must not be empty");
    }

    std::set<Network> seen_networks;
    for (std::size_t index = 0; index < config.networks.size(); ++index) {
        const BlockchainNetworkConfig& network = config.networks[index];
        const std::string prefix = "blockchain.networks[" + std::to_string(index) + "].";
        if (!seen_networks.insert(network.network).second) {
            add(prefix + "network is duplicated");
        }
        if (!isValidRpcUrl(network.rpc_url)) {
            add(prefix + "rpc_url must be an http, https, ws, or wss URL");
        }
        if (!isValidAddress(network.identity_registry)) {
            add(prefix + "identity_registry must be a 0x-prefixed 20-byte address");
        }
        if (!isValidAddress(network.reputation_registry)) {
            add(prefix + "reputation_registry must be a 0x-prefixed 20-byte address");
        }
        if (!isValidAddress(network.validation_registry)) {
            add(prefix + "validation_registry must be a 0x-prefixed 20-byte address");
        }
        if (network.gas_limit == 0) {
            add(prefix + "gas_limit must be > 0");
        }
    }

    const auto check_endpoint = [&add](const EndpointConfig& endpoint, const std::string& prefix, bool require_path) {
        if (require_path && endpoint.path_pattern.empty()) {
            add(prefix + "path_pattern is required");
        }
        if (endpoint.price_usdc < 0.0) {
            add(prefix + "price_usdc must be >= 0");
        }
        if (endpoint.trust_threshold < 0.0 || endpoint.trust_threshold > 1.0) {
            add(prefix + "trust_threshold must be in [0, 1]");
        }
        if (!isValidHttpUrl(endpoint.backend_url)) {
            add(prefix + "backend_url must be an http or https URL");
        }
        if (endpoint.backend_timeout.count() <= 0) {
            add(prefix + "backend_timeout must be >= 1");
        }
    };
    for (std::size_t index = 0; index < config.endpoints.size(); ++index) {
        check_endpoint(config.endpoints[index], "endpoints[" + std::to_string(index) + "].", true);
    }
    check_endpoint(config.default_endpoint, "default_endpoint.", false);

    if (config.cache.max_size_mb == 0) {
        add("cache.max_size_mb must be > 0");
    }
    if (config.security.bloom_filter_size_mb == 0) {
        add("security.bloom_filter_size_mb must be > 0");
    }
    if (config.security.bloom_filter_retention.count() <= 0) {
        add("security.bloom_filter_retention_hours must be > 0");
    }
    if (!isValidLogLevel(config.monitoring.log_level)) {
        add("monitoring.log_level must be trace, debug, info, warn, or error");
    }
    return errors;
}

std::string ConfigurationManager::serialize(const Configuration& config) {
    const auto proofs_of = [](const std::vector<ProofType>& proofs) {
        toml::array values;
        for (const ProofType proof : proofs) {
            values.push_back(ProofTypeToString(proof));
        }
        return values;
    };
    const auto endpoint_table = [&proofs_of](const EndpointConfig& endpoint, bool include_path) {
        toml::table table;
        if (include_path) {
            table.insert("path_pattern", endpoint.path_pattern);
        }
        table.insert("price_usdc", endpoint.price_usdc);
        table.insert("trust_threshold", endpoint.trust_threshold);
        table.insert("required_proofs", proofs_of(endpoint.required_proofs));
        table.insert("backend_url", endpoint.backend_url);
        table.insert("backend_timeout", static_cast<std::int64_t>(endpoint.backend_timeout.count()));
        table.insert("rate_limit_per_minute", static_cast<std::int64_t>(endpoint.rate_limit_per_minute));
        table.insert("new_agents_allowed", endpoint.new_agents_allowed);
        return table;
    };

    toml::table server;
    server.insert("listen_address", config.server.listen_address);
    server.insert("listen_port", static_cast<std::int64_t>(config.server.listen_port));
    server.insert("monitoring_port", static_cast<std::int64_t>(config.server.monitoring_port));
    server.insert("thread_pool_size", static_cast<std::int64_t>(config.server.thread_pool_size));

    toml::array networks;
    for (const BlockchainNetworkConfig& network : config.networks) {
        toml::table row;
        row.insert("network", NetworkToString(network.network));
        row.insert("rpc_url", network.rpc_url);
        row.insert("identity_registry", network.identity_registry);
        row.insert("reputation_registry", network.reputation_registry);
        row.insert("validation_registry", network.validation_registry);
        row.insert("gas_limit", static_cast<std::int64_t>(network.gas_limit));
        row.insert("enabled", network.enabled);
        networks.push_back(std::move(row));
    }
    toml::table blockchain;
    blockchain.insert("payment_wallet_seed", config.payment_wallet_seed);
    blockchain.insert("networks", std::move(networks));

    toml::array endpoints;
    for (const EndpointConfig& endpoint : config.endpoints) {
        endpoints.push_back(endpoint_table(endpoint, true));
    }

    toml::table cache;
    cache.insert("max_size_mb", static_cast<std::int64_t>(config.cache.max_size_mb));
    cache.insert("default_ttl_seconds", static_cast<std::int64_t>(config.cache.default_ttl.count()));

    toml::table security;
    security.insert("bloom_filter_size_mb", static_cast<std::int64_t>(config.security.bloom_filter_size_mb));
    security.insert("bloom_filter_retention_hours",
                    static_cast<std::int64_t>(config.security.bloom_filter_retention.count()));

    toml::table monitoring;
    monitoring.insert("prometheus_enabled", config.monitoring.prometheus_enabled);
    monitoring.insert("opentelemetry_enabled", config.monitoring.opentelemetry_enabled);
    monitoring.insert("log_level", config.monitoring.log_level);

    toml::table root;
    root.insert("server", std::move(server));
    root.insert("blockchain", std::move(blockchain));
    root.insert("endpoints", std::move(endpoints));
    root.insert("default_endpoint", endpoint_table(config.default_endpoint, false));
    root.insert("cache", std::move(cache));
    root.insert("security", std::move(security));
    root.insert("monitoring", std::move(monitoring));

    std::ostringstream output;
    output << root;
    return output.str();
}

ConfigurationWatcher::ConfigurationWatcher(std::filesystem::path path,
                                           std::function<void(Configuration)> on_reload,
                                           std::chrono::milliseconds poll_interval)
    : path_(std::move(path))
    , on_reload_(std::move(on_reload))
    , poll_interval_(poll_interval)
    , thread_([this] { run(); }) {}

ConfigurationWatcher::~ConfigurationWatcher() {
    stop_.store(true);
    wait_cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ConfigurationWatcher::run() {
    std::filesystem::file_time_type seen_time{};
    std::uintmax_t seen_size = 0;
    bool have_stamp = false;
    while (!stop_.load()) {
        std::error_code error;
        if (std::filesystem::exists(path_, error) && !error) {
            const auto write_time = std::filesystem::last_write_time(path_, error);
            const auto size = std::filesystem::file_size(path_, error);
            if (!error && (!have_stamp || write_time != seen_time || size != seen_size)) {
                const auto loaded = ConfigurationManager::loadFromFile(path_);
                if (loaded && ConfigurationManager::validate(loaded.value()).empty()) {
                    have_stamp = true;
                    seen_time = write_time;
                    seen_size = size;
                    on_reload_(loaded.value());
                }
            }
        }
        std::unique_lock lock(wait_mutex_);
        wait_cv_.wait_for(lock, poll_interval_, [this] { return stop_.load(); });
    }
}

std::unique_ptr<ConfigurationWatcher> ConfigurationManager::watchForChanges(
    const std::filesystem::path& config_path,
    std::function<void(Configuration)> on_reload,
    std::chrono::milliseconds poll_interval) {
    return std::make_unique<ConfigurationWatcher>(config_path, std::move(on_reload), poll_interval);
}

}  // namespace gateway::core
