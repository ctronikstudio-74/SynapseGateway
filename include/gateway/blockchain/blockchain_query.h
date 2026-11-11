#pragma once

#include "gateway/blockchain/models.h"
#include "gateway/core/Configuration.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace gateway::blockchain {

enum class TxStatus { Found, MissingMetadata, NotFound, NetworkUnavailable };

struct TxLookup {
    TxStatus status = TxStatus::NotFound;
    std::optional<Transaction> transaction;
};

enum class LookupStatus { Found, NotFound, Unavailable, Error };

struct IdentityLookup {
    LookupStatus status = LookupStatus::NotFound;
    std::optional<IdentityNFT> nft;
};

struct ReputationLookup {
    LookupStatus status = LookupStatus::NotFound;
    std::optional<ReputationData> reputation;
};

struct ProofResult {
    bool valid = false;
    std::chrono::milliseconds elapsed{0};
    std::string detail;
};

class BlockchainQuery {
public:
    virtual ~BlockchainQuery() = default;

    virtual TxLookup getTransaction(const std::string& tx_hash, gateway::core::Network network) = 0;
    virtual IdentityLookup getIdentityNFT(const std::string& wallet_address,
                                          gateway::core::Network network) = 0;
    virtual ReputationLookup getReputationData(const std::string& wallet_address,
                                               gateway::core::Network network) = 0;
    virtual ProofResult verifyProofOfWork(const std::string& wallet_address,
                                          gateway::core::ProofType proof_type,
                                          gateway::core::Network network,
                                          const std::string& proof_hex,
                                          std::uint64_t min_stake,
                                          std::uint64_t now_unix) = 0;
    virtual double getGasFeeEstimate(gateway::core::Network network) const = 0;
    virtual bool isNetworkHealthy(gateway::core::Network network) const = 0;
    virtual bool supportsNetwork(gateway::core::Network network) const = 0;
    virtual std::optional<gateway::core::Network> selectOptimalNetwork() const = 0;
    virtual std::string paymentWallet() const = 0;

    /**
     * @brief Drop RPC clients during shutdown. Later queries report the network unavailable.
     */
    virtual void closeConnections() {}
};

}  // namespace gateway::blockchain
