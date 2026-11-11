#pragma once

#include "gateway/blockchain/blockchain_query.h"

#include <string>
#include <utility>

namespace gateway::test {

class FakeChain final : public gateway::blockchain::BlockchainQuery {
public:
    std::string wallet = "0x1111111111111111111111111111111111111111";
    bool supported = true;
    bool healthy = true;
    double gas_fee = 1.0;
    std::optional<gateway::core::Network> optimal = gateway::core::Network::BASE;
    gateway::blockchain::TxLookup transaction{};
    gateway::blockchain::IdentityLookup identity{};
    gateway::blockchain::ReputationLookup reputation{};
    gateway::blockchain::ProofResult proof{};
    int transaction_queries = 0;
    int identity_queries = 0;
    int reputation_queries = 0;

    gateway::blockchain::TxLookup getTransaction(const std::string&, gateway::core::Network) override {
        ++transaction_queries;
        return transaction;
    }

    gateway::blockchain::IdentityLookup getIdentityNFT(const std::string&, gateway::core::Network) override {
        ++identity_queries;
        return identity;
    }

    gateway::blockchain::ReputationLookup getReputationData(const std::string&, gateway::core::Network) override {
        ++reputation_queries;
        return reputation;
    }

    gateway::blockchain::ProofResult verifyProofOfWork(const std::string&,
                                                       gateway::core::ProofType,
                                                       gateway::core::Network,
                                                       const std::string&,
                                                       std::uint64_t,
                                                       std::uint64_t) override {
        return proof;
    }

    double getGasFeeEstimate(gateway::core::Network) const override { return gas_fee; }
    bool isNetworkHealthy(gateway::core::Network) const override { return healthy; }
    bool supportsNetwork(gateway::core::Network) const override { return supported; }
    std::optional<gateway::core::Network> selectOptimalNetwork() const override { return optimal; }
    std::string paymentWallet() const override { return wallet; }
};

}  // namespace gateway::test
