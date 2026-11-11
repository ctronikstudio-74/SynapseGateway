#pragma once

#include "gateway/blockchain/blockchain_query.h"
#include "gateway/blockchain/rpc_client.h"
#include "gateway/core/Configuration.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace gateway::blockchain {

class BlockchainConnector final : public BlockchainQuery {
public:
    BlockchainConnector(std::vector<gateway::core::BlockchainNetworkConfig> networks,
                        std::string wallet_seed,
                        std::shared_ptr<RpcClient> rpc);

    void initialize();
    void reloadNetworkConfigs(std::vector<gateway::core::BlockchainNetworkConfig> networks);
    void refreshGasFees();
    void refreshHealth();

    TxLookup getTransaction(const std::string& tx_hash, gateway::core::Network network) override;
    IdentityLookup getIdentityNFT(const std::string& wallet_address,
                                  gateway::core::Network network) override;
    ReputationLookup getReputationData(const std::string& wallet_address,
                                       gateway::core::Network network) override;
    ProofResult verifyProofOfWork(const std::string& wallet_address,
                                  gateway::core::ProofType proof_type,
                                  gateway::core::Network network,
                                  const std::string& proof_hex,
                                  std::uint64_t min_stake,
                                  std::uint64_t now_unix) override;
    double getGasFeeEstimate(gateway::core::Network network) const override;
    bool isNetworkHealthy(gateway::core::Network network) const override;
    bool supportsNetwork(gateway::core::Network network) const override;
    std::optional<gateway::core::Network> selectOptimalNetwork() const override;
    std::string paymentWallet() const override;
    void closeConnections() override;

private:
    struct NetworkState {
        gateway::core::BlockchainNetworkConfig config;
        bool healthy = true;
        double gas_fee_gwei = 0.0;
    };

    [[nodiscard]] std::string rpcUrl(gateway::core::Network network) const;
    [[nodiscard]] const NetworkState* findState(gateway::core::Network network) const;
    void markUnhealthy(gateway::core::Network network);
    [[nodiscard]] nlohmann::json ethCall(gateway::core::Network network,
                                         const std::string& contract,
                                         const std::string& data) const;

    std::string wallet_seed_;
    std::shared_ptr<RpcClient> rpc_;
    mutable std::shared_mutex mutex_;
    std::map<gateway::core::Network, NetworkState> networks_;
    std::atomic<bool> closed_{false};
};

}  // namespace gateway::blockchain
