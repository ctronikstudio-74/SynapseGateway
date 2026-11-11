#include "gateway/blockchain/BlockchainConnector.h"

#include "gateway/blockchain/abi_decoder.h"
#include "gateway/blockchain/create2.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/blockchain/network_selection.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace gateway::blockchain {
namespace {

constexpr std::uint64_t kTeeMaxAgeSeconds = 7ULL * 24ULL * 60ULL * 60ULL;

std::string asString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return {};
}

std::uint64_t confirmationsRequired(gateway::core::Network network) {
    return network == gateway::core::Network::ETHEREUM_MAINNET ? 12 : 1;
}

std::string transferTopic() {
    const auto hash = keccak256("Transfer(address,address,uint256)");
    return "0x" + toHex(hash);
}

bool sameHex(std::string_view left, std::string_view right) {
    return normalizeAddress(left) == normalizeAddress(right);
}

std::string topicAddress(std::string_view topic) {
    const auto bytes = fromHex(topic);
    if (bytes.size() < 20) {
        return {};
    }
    return "0x" + toHex(std::span<const std::uint8_t>(bytes.data() + (bytes.size() - 20), 20));
}

std::string decimalLess(const std::string& left, const std::string& right) {
    const auto trim = [](std::string value) {
        const auto nonzero = value.find_first_not_of('0');
        if (nonzero == std::string::npos) {
            return std::string("0");
        }
        return value.substr(nonzero);
    };
    const std::string a = trim(left);
    const std::string b = trim(right);
    if (a.size() != b.size()) {
        return a.size() > b.size() ? a : b;
    }
    return a >= b ? a : b;
}

}  // namespace

BlockchainConnector::BlockchainConnector(std::vector<gateway::core::BlockchainNetworkConfig> networks,
                                         std::string wallet_seed,
                                         std::shared_ptr<RpcClient> rpc)
    : wallet_seed_(std::move(wallet_seed)), rpc_(std::move(rpc)) {
    reloadNetworkConfigs(std::move(networks));
}

void BlockchainConnector::reloadNetworkConfigs(std::vector<gateway::core::BlockchainNetworkConfig> networks) {
    std::unique_lock lock(mutex_);
    std::map<gateway::core::Network, NetworkState> replacement;
    for (auto& network : networks) {
        NetworkState& slot = replacement[network.network];
        const auto previous = networks_.find(network.network);
        if (previous != networks_.end()) {
            slot.healthy = previous->second.healthy;
            slot.gas_fee_gwei = previous->second.gas_fee_gwei;
        }
        slot.config = std::move(network);
    }
    networks_ = std::move(replacement);
}

void BlockchainConnector::initialize() {
    refreshHealth();
    refreshGasFees();
}

std::string BlockchainConnector::paymentWallet() const {
    return generateDeterministicWallet(wallet_seed_, gateway::core::Network::BASE);
}

bool BlockchainConnector::supportsNetwork(gateway::core::Network network) const {
    if (closed_.load()) {
        return false;
    }
    std::shared_lock lock(mutex_);
    const auto it = networks_.find(network);
    return it != networks_.end() && it->second.config.enabled;
}

bool BlockchainConnector::isNetworkHealthy(gateway::core::Network network) const {
    if (closed_.load()) {
        return false;
    }
    std::shared_lock lock(mutex_);
    const auto it = networks_.find(network);
    return it != networks_.end() && it->second.config.enabled && it->second.healthy;
}

void BlockchainConnector::closeConnections() {
    std::unique_lock lock(mutex_);
    closed_.store(true);
    for (auto& entry : networks_) {
        entry.second.healthy = false;
    }
}

double BlockchainConnector::getGasFeeEstimate(gateway::core::Network network) const {
    std::shared_lock lock(mutex_);
    const auto it = networks_.find(network);
    if (it == networks_.end()) {
        return 0.0;
    }
    return it->second.gas_fee_gwei;
}

std::optional<gateway::core::Network> BlockchainConnector::selectOptimalNetwork() const {
    std::vector<NetworkCandidate> candidates;
    {
        std::shared_lock lock(mutex_);
        candidates.reserve(networks_.size());
        for (const auto& [network, state] : networks_) {
            candidates.push_back(NetworkCandidate{network, state.gas_fee_gwei, state.healthy, state.config.enabled});
        }
    }
    return gateway::blockchain::selectOptimalNetwork(candidates);
}

std::string BlockchainConnector::rpcUrl(gateway::core::Network network) const {
    std::shared_lock lock(mutex_);
    const auto it = networks_.find(network);
    if (it == networks_.end() || !it->second.config.enabled) {
        throw std::out_of_range("network is not configured");
    }
    return it->second.config.rpc_url;
}

void BlockchainConnector::markUnhealthy(gateway::core::Network network) {
    std::unique_lock lock(mutex_);
    const auto it = networks_.find(network);
    if (it != networks_.end()) {
        it->second.healthy = false;
    }
}

nlohmann::json BlockchainConnector::ethCall(gateway::core::Network network,
                                            const std::string& contract,
                                            const std::string& data) const {
    const nlohmann::json transaction = {{"to", contract}, {"data", data}};
    return rpc_->call(rpcUrl(network), "eth_call", nlohmann::json::array({transaction, "latest"}));
}

void BlockchainConnector::refreshHealth() {
    if (closed_.load()) {
        return;
    }
    std::vector<std::pair<gateway::core::Network, std::string>> targets;
    {
        std::shared_lock lock(mutex_);
        for (const auto& [network, state] : networks_) {
            if (state.config.enabled) {
                targets.emplace_back(network, state.config.rpc_url);
            }
        }
    }
    for (const auto& [network, url] : targets) {
        try {
            rpc_->call(url, "eth_blockNumber", nlohmann::json::array());
            std::unique_lock lock(mutex_);
            networks_.at(network).healthy = true;
        } catch (const std::exception&) {
            markUnhealthy(network);
        }
    }
}

void BlockchainConnector::refreshGasFees() {
    if (closed_.load()) {
        return;
    }
    std::vector<std::pair<gateway::core::Network, std::string>> targets;
    {
        std::shared_lock lock(mutex_);
        for (const auto& [network, state] : networks_) {
            if (state.config.enabled) {
                targets.emplace_back(network, state.config.rpc_url);
            }
        }
    }
    for (const auto& [network, url] : targets) {
        try {
            const nlohmann::json price = rpc_->call(url, "eth_gasPrice", nlohmann::json::array());
            const double wei = std::stod(hexToDecimal(asString(price)));
            std::unique_lock lock(mutex_);
            networks_.at(network).gas_fee_gwei = wei / 1'000'000'000.0;
            networks_.at(network).healthy = true;
        } catch (const std::exception&) {
            markUnhealthy(network);
        }
    }
}

TxLookup BlockchainConnector::getTransaction(const std::string& tx_hash, gateway::core::Network network) {
    if (tx_hash.empty()) {
        return TxLookup{TxStatus::MissingMetadata, std::nullopt};
    }
    if (!supportsNetwork(network)) {
        return TxLookup{TxStatus::NetworkUnavailable, std::nullopt};
    }

    try {
        const std::string url = rpcUrl(network);
        const nlohmann::json tx = rpc_->call(url, "eth_getTransactionByHash", nlohmann::json::array({tx_hash}));
        if (tx.is_null()) {
            return TxLookup{TxStatus::NotFound, std::nullopt};
        }
        const std::string from = asString(tx.value("from", nlohmann::json{}));
        std::string to = asString(tx.value("to", nlohmann::json{}));
        std::string value = tx.contains("value") && tx["value"].is_string()
                                ? hexToDecimal(tx["value"].get<std::string>())
                                : std::string{};

        const nlohmann::json receipt =
            rpc_->call(url, "eth_getTransactionReceipt", nlohmann::json::array({tx_hash}));
        std::string block_quantity = asString(tx.value("blockNumber", nlohmann::json{}));
        if (receipt.is_object() && receipt.contains("logs") && receipt["logs"].is_array()) {
            const std::string topic = transferTopic();
            std::string best_amount = "0";
            std::string best_to;
            for (const auto& log : receipt["logs"]) {
                if (!log.contains("topics") || !log["topics"].is_array() || log["topics"].size() < 3) {
                    continue;
                }
                if (!sameHex(asString(log["topics"][0]), topic)) {
                    continue;
                }
                const std::string recipient = topicAddress(asString(log["topics"][2]));
                const std::string amount =
                    log.contains("data") ? hexToDecimal(asString(log["data"])) : std::string("0");
                if (decimalLess(best_amount, amount) == amount && amount != "0") {
                    best_amount = amount;
                    best_to = recipient;
                }
            }
            if (!best_to.empty()) {
                to = best_to;
                value = best_amount;
            }
            if (block_quantity.empty()) {
                block_quantity = asString(receipt.value("blockNumber", nlohmann::json{}));
            }
        }
        if (from.empty() || to.empty() || value.empty()) {
            return TxLookup{TxStatus::MissingMetadata, std::nullopt};
        }

        Transaction parsed;
        parsed.hash = tx_hash;
        parsed.from_address = from;
        parsed.to_address = to;
        parsed.value_wei = value;
        parsed.block_number = block_quantity.empty() ? 0 : hexToUint64(block_quantity);
        parsed.execution_succeeded = true;
        if (receipt.is_object()) {
            const std::string status = asString(receipt.value("status", nlohmann::json{}));
            parsed.execution_succeeded = status.empty() || hexToUint64(status) == 1;
        }

        const nlohmann::json latest = rpc_->call(url, "eth_blockNumber", nlohmann::json::array());
        const std::uint64_t latest_block = hexToUint64(asString(latest));
        if (parsed.block_number > 0 && !block_quantity.empty()) {
            const nlohmann::json block =
                rpc_->call(url, "eth_getBlockByNumber", nlohmann::json::array({block_quantity, false}));
            if (block.is_object()) {
                parsed.timestamp = hexToUint64(asString(block.value("timestamp", nlohmann::json{})));
            }
            const std::uint64_t depth =
                latest_block >= parsed.block_number ? (latest_block - parsed.block_number + 1) : 0;
            parsed.is_finalized = parsed.execution_succeeded && depth >= confirmationsRequired(network);
        }
        return TxLookup{TxStatus::Found, std::move(parsed)};
    } catch (const std::exception&) {
        markUnhealthy(network);
        return TxLookup{TxStatus::NetworkUnavailable, std::nullopt};
    }
}

IdentityLookup BlockchainConnector::getIdentityNFT(const std::string& wallet_address,
                                                   gateway::core::Network network) {
    if (!supportsNetwork(network)) {
        return IdentityLookup{LookupStatus::Unavailable, std::nullopt};
    }
    try {
        std::string registry;
        {
            std::shared_lock lock(mutex_);
            registry = networks_.at(network).config.identity_registry;
        }
        const auto balance_data =
            AbiDecoder::encodeCall("balanceOf(address)", {AbiDecoder::encodeAddress(wallet_address)});
        const auto balance = ethCall(network, registry, balance_data);
        if (AbiDecoder::decodeUint64(asString(balance), 0) == 0) {
            return IdentityLookup{LookupStatus::NotFound, std::nullopt};
        }
        const auto token_data = AbiDecoder::encodeCall(
            "tokenOfOwnerByIndex(address,uint256)",
            {AbiDecoder::encodeAddress(wallet_address), AbiDecoder::encodeUint256(0)});
        const auto token = ethCall(network, registry, token_data);
        const auto token_id = AbiDecoder::decodeUint64(asString(token), 0);
        const auto identity_data =
            AbiDecoder::encodeCall("getIdentityData(uint256)", {AbiDecoder::encodeUint256(token_id)});
        const auto encoded = ethCall(network, registry, identity_data);
        IdentityNFT nft = AbiDecoder::decodeIdentityData(asString(encoded), std::to_string(token_id), wallet_address);
        return IdentityLookup{LookupStatus::Found, std::move(nft)};
    } catch (const std::exception&) {
        markUnhealthy(network);
        return IdentityLookup{LookupStatus::Error, std::nullopt};
    }
}

ReputationLookup BlockchainConnector::getReputationData(const std::string& wallet_address,
                                                        gateway::core::Network network) {
    if (!supportsNetwork(network)) {
        return ReputationLookup{LookupStatus::Unavailable, std::nullopt};
    }
    try {
        std::string registry;
        {
            std::shared_lock lock(mutex_);
            registry = networks_.at(network).config.reputation_registry;
        }
        const auto data =
            AbiDecoder::encodeCall("getReputation(address)", {AbiDecoder::encodeAddress(wallet_address)});
        const auto encoded = ethCall(network, registry, data);
        const std::string hex = asString(encoded);
        if (hex.empty() || AbiDecoder::decodeUint64(hex, 3) == 0) {
            return ReputationLookup{LookupStatus::NotFound, std::nullopt};
        }
        return ReputationLookup{LookupStatus::Found, AbiDecoder::decodeReputation(hex, wallet_address)};
    } catch (const std::exception&) {
        markUnhealthy(network);
        return ReputationLookup{LookupStatus::Error, std::nullopt};
    }
}

ProofResult BlockchainConnector::verifyProofOfWork(const std::string& wallet_address,
                                                   gateway::core::ProofType proof_type,
                                                   gateway::core::Network network,
                                                   const std::string& proof_hex,
                                                   std::uint64_t min_stake,
                                                   std::uint64_t now_unix) {
    const auto started = std::chrono::steady_clock::now();
    ProofResult result;
    auto finish = [&](bool valid, std::string detail) {
        result.valid = valid;
        result.detail = std::move(detail);
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        return result;
    };

    if (proof_type == gateway::core::ProofType::NONE) {
        return finish(true, "no proof required");
    }
    if (!supportsNetwork(network)) {
        return finish(false, "network unavailable");
    }
    if (now_unix == 0) {
        now_unix = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    try {
        std::string registry;
        {
            std::shared_lock lock(mutex_);
            registry = networks_.at(network).config.validation_registry;
        }
        if (proof_type == gateway::core::ProofType::ZK_ML) {
            const auto data = AbiDecoder::encodeCall(
                "verifyZkMLProof(address,bytes32)",
                {AbiDecoder::encodeAddress(wallet_address), AbiDecoder::encodeBytes32(proof_hex)});
            const auto encoded = ethCall(network, registry, data);
            const bool valid = AbiDecoder::decodeBool(asString(encoded), 0);
            return finish(valid, valid ? "zkml proof accepted" : "zkml proof rejected");
        }
        if (proof_type == gateway::core::ProofType::TEE_ATTESTATION) {
            const auto data = AbiDecoder::encodeDynamicBytesCall("verifyTEEAttestation(address,bytes)", wallet_address,
                                                                 proof_hex);
            const auto encoded = ethCall(network, registry, data);
            const bool valid = AbiDecoder::decodeBool(asString(encoded), 0);
            const auto attested_at = AbiDecoder::decodeUint64(asString(encoded), 1);
            if (!valid) {
                return finish(false, "tee attestation rejected");
            }
            if (attested_at > now_unix || now_unix - attested_at > kTeeMaxAgeSeconds) {
                return finish(false, "tee attestation is older than 7 days");
            }
            return finish(true, "tee attestation accepted");
        }
        const auto data = AbiDecoder::encodeCall("getStake(address)", {AbiDecoder::encodeAddress(wallet_address)});
        const auto encoded = ethCall(network, registry, data);
        const auto stake = AbiDecoder::decodeUint64(asString(encoded), 0);
        const bool valid = stake >= min_stake;
        return finish(valid, valid ? "stake accepted" : "stake below minimum");
    } catch (const std::exception& ex) {
        markUnhealthy(network);
        return finish(false, ex.what());
    }
}

}  // namespace gateway::blockchain
