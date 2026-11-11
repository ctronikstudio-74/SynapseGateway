#include "gateway/core/Configuration.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace gateway::core {

namespace {

// Network enum string mappings
const std::unordered_map<std::string, Network> kNetworkFromString = {
    {"ethereum_mainnet", Network::ETHEREUM_MAINNET},
    {"ethereum", Network::ETHEREUM_MAINNET},
    {"base", Network::BASE},
    {"arbitrum", Network::ARBITRUM},
    {"polygon", Network::POLYGON},
    {"bnb_chain", Network::BNB_CHAIN},
    {"bnb", Network::BNB_CHAIN}
};

const std::unordered_map<Network, std::string> kNetworkToString = {
    {Network::ETHEREUM_MAINNET, "ethereum_mainnet"},
    {Network::BASE, "base"},
    {Network::ARBITRUM, "arbitrum"},
    {Network::POLYGON, "polygon"},
    {Network::BNB_CHAIN, "bnb_chain"}
};

// ProofType enum string mappings
const std::unordered_map<std::string, ProofType> kProofTypeFromString = {
    {"none", ProofType::NONE},
    {"zkml", ProofType::ZK_ML},
    {"zk_ml", ProofType::ZK_ML},
    {"tee", ProofType::TEE_ATTESTATION},
    {"tee_attestation", ProofType::TEE_ATTESTATION},
    {"staking", ProofType::STAKING}
};

const std::unordered_map<ProofType, std::string> kProofTypeToString = {
    {ProofType::NONE, "none"},
    {ProofType::ZK_ML, "zkml"},
    {ProofType::TEE_ATTESTATION, "tee_attestation"},
    {ProofType::STAKING, "staking"}
};

} // anonymous namespace

/**
 * @brief Convert string to Network enum
 * @param str Network name string (case-insensitive)
 * @return Network enum value
 * @throws std::invalid_argument if string is not a valid network name
 */
Network NetworkFromString(const std::string& str) {
    std::string lower = str;
    // Convert to lowercase for case-insensitive matching
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    auto it = kNetworkFromString.find(lower);
    if (it == kNetworkFromString.end()) {
        throw std::invalid_argument("Invalid network name: " + str);
    }
    return it->second;
}

/**
 * @brief Convert Network enum to string
 * @param network Network enum value
 * @return Network name string
 */
std::string NetworkToString(Network network) {
    auto it = kNetworkToString.find(network);
    if (it == kNetworkToString.end()) {
        throw std::invalid_argument("Invalid network enum value");
    }
    return it->second;
}

/**
 * @brief Convert string to ProofType enum
 * @param str Proof type string (case-insensitive)
 * @return ProofType enum value
 * @throws std::invalid_argument if string is not a valid proof type
 */
ProofType ProofTypeFromString(const std::string& str) {
    std::string lower = str;
    // Convert to lowercase for case-insensitive matching
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    auto it = kProofTypeFromString.find(lower);
    if (it == kProofTypeFromString.end()) {
        throw std::invalid_argument("Invalid proof type: " + str);
    }
    return it->second;
}

/**
 * @brief Convert ProofType enum to string
 * @param proof_type ProofType enum value
 * @return Proof type string
 */
std::string ProofTypeToString(ProofType proof_type) {
    auto it = kProofTypeToString.find(proof_type);
    if (it == kProofTypeToString.end()) {
        throw std::invalid_argument("Invalid proof type enum value");
    }
    return it->second;
}

} // namespace gateway::core
