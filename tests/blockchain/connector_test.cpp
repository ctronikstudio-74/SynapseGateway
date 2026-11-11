#include "gateway/blockchain/BlockchainConnector.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/blockchain/rpc_client.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>

namespace {

class ScriptedRpc final : public gateway::blockchain::RpcClient {
public:
    std::function<nlohmann::json(const std::string& method, const nlohmann::json& params)> handler =
        [](const std::string&, const nlohmann::json&) { return nlohmann::json{}; };

    nlohmann::json call(const std::string&, const std::string& method, const nlohmann::json& params) override {
        ++calls;
        return handler(method, params);
    }

    int calls = 0;
};

gateway::core::BlockchainNetworkConfig networkConfig(gateway::core::Network network, bool enabled) {
    gateway::core::BlockchainNetworkConfig config;
    config.network = network;
    config.rpc_url = "https://rpc.example/" + gateway::core::NetworkToString(network);
    config.identity_registry = "0x0000000000000000000000000000000000000001";
    config.reputation_registry = "0x0000000000000000000000000000000000000002";
    config.validation_registry = "0x0000000000000000000000000000000000000003";
    config.gas_limit = 100000;
    config.enabled = enabled;
    return config;
}

}  // namespace

TEST(HttpRpcClientTest, RejectsMalformedUrl) {
    gateway::blockchain::HttpRpcClient client(1);
    EXPECT_THROW(client.call("notaurl", "eth_blockNumber", nlohmann::json::array()), std::runtime_error);
}

TEST(BlockchainConnectorTest, MissingTransactionMetadataIsRejected) {
    auto rpc = std::make_shared<ScriptedRpc>();
    gateway::blockchain::BlockchainConnector connector({networkConfig(gateway::core::Network::BASE, true)}, "seed", rpc);
    const auto lookup = connector.getTransaction("", gateway::core::Network::BASE);
    EXPECT_EQ(lookup.status, gateway::blockchain::TxStatus::MissingMetadata);
    EXPECT_EQ(rpc->calls, 0);
}

TEST(BlockchainConnectorTest, ParsesFinalizedTransferAndSelectsCheapestHealthyNetwork) {
    auto rpc = std::make_shared<ScriptedRpc>();
    rpc->handler = [](const std::string& method, const nlohmann::json&) {
        if (method == "eth_gasPrice") {
            return nlohmann::json("0x3b9aca00");
        }
        if (method == "eth_blockNumber") {
            return nlohmann::json("0x10");
        }
        if (method == "eth_getTransactionByHash") {
            return nlohmann::json{{"from", "0x2222222222222222222222222222222222222222"},
                                  {"to", "0x3333333333333333333333333333333333333333"},
                                  {"value", "0x0"},
                                  {"blockNumber", "0x10"}};
        }
        if (method == "eth_getTransactionReceipt") {
            const auto topic = gateway::blockchain::toHex(
                gateway::blockchain::keccak256("Transfer(address,address,uint256)"));
            const std::string recipient =
                "0x0000000000000000000000001111111111111111111111111111111111111111";
            return nlohmann::json{{"status", "0x1"},
                                  {"blockNumber", "0x10"},
                                  {"logs",
                                   nlohmann::json::array({{{"topics", {"0x" + topic, "0x" + std::string(64, '0'), recipient}},
                                                           {"data", "0x3e8"}}})}};
        }
        if (method == "eth_getBlockByNumber") {
            return nlohmann::json{{"timestamp", "0x65"}};
        }
        return nlohmann::json("0x1");
    };

    gateway::blockchain::BlockchainConnector connector(
        {networkConfig(gateway::core::Network::BASE, true), networkConfig(gateway::core::Network::ARBITRUM, true)},
        "seed", rpc);
    connector.refreshGasFees();
    connector.refreshHealth();
    const auto selected = connector.selectOptimalNetwork();
    ASSERT_TRUE(selected);
    EXPECT_TRUE(connector.isNetworkHealthy(*selected));

    const auto lookup = connector.getTransaction("0xabc", gateway::core::Network::BASE);
    ASSERT_EQ(lookup.status, gateway::blockchain::TxStatus::Found);
    ASSERT_TRUE(lookup.transaction);
    EXPECT_EQ(lookup.transaction->value_wei, "1000");
    EXPECT_EQ(lookup.transaction->to_address, "0x1111111111111111111111111111111111111111");
    EXPECT_TRUE(lookup.transaction->is_finalized);
    EXPECT_EQ(lookup.transaction->timestamp, 0x65u);
}

TEST(BlockchainConnectorTest, FailedRpcMarksNetworkUnhealthy) {
    auto rpc = std::make_shared<ScriptedRpc>();
    rpc->handler = [](const std::string&, const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("down");
    };
    gateway::blockchain::BlockchainConnector connector({networkConfig(gateway::core::Network::BASE, true)}, "seed", rpc);
    connector.refreshHealth();
    EXPECT_FALSE(connector.isNetworkHealthy(gateway::core::Network::BASE));
    EXPECT_FALSE(connector.selectOptimalNetwork().has_value());
}
