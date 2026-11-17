#pragma once

#include "gateway/blockchain/models.h"
#include "gateway/core/Configuration.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

struct TransferAuthorization {
    std::string from;
    std::string to;
    std::string value_units;
    std::uint64_t valid_after = 0;
    std::uint64_t valid_before = 0;
    std::string nonce_hex;
    std::string signature_hex;
};

struct Broadcast {
    enum class Status { Submitted, NotConfigured, Rejected, Unavailable };

    Status status = Status::NotConfigured;
    std::string hash;
    std::string detail;
};

class BlockchainQuery {
public:
    virtual ~BlockchainQuery() = default;

    virtual TxLookup getTransaction(const std::string& tx_hash, gateway::core::Network network) = 0;

    /**
     * @brief Payment lookup that may bind the transfer to the paying agent wallet.
     *
     * The default reads the same transaction. Connectors that know the settlement
     * token override this and ignore transfers from any other contract or sender.
     */
    virtual TxLookup getPaymentTransfer(const std::string& tx_hash,
                                        gateway::core::Network network,
                                        std::string_view payer) {
        (void)payer;
        return getTransaction(tx_hash, network);
    }

    virtual TxLookup getTokenTransfer(const std::string& tx_hash,
                                      gateway::core::Network network,
                                      std::string_view token,
                                      std::string_view sender,
                                      std::string_view recipient) {
        (void)token;
        (void)sender;
        (void)recipient;
        return getTransaction(tx_hash, network);
    }

    virtual Broadcast submitTransferAuthorization(gateway::core::Network network,
                                                  const TransferAuthorization& authorization) {
        (void)network;
        (void)authorization;
        return {};
    }

    virtual Broadcast submitErc20Transfer(gateway::core::Network network,
                                          std::string_view token,
                                          std::string_view recipient,
                                          std::string_view value_units) {
        (void)network;
        (void)token;
        (void)recipient;
        (void)value_units;
        return {};
    }
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

    /**
     * @brief ERC-1271 check for a contract wallet. EOA recovery does not use this.
     */
    virtual bool isValidContractSignature(const std::string& wallet,
                                          const std::string& digest_hex,
                                          const std::string& signature_hex,
                                          gateway::core::Network network) {
        (void)wallet;
        (void)digest_hex;
        (void)signature_hex;
        (void)network;
        return false;
    }
};

}  // namespace gateway::blockchain
