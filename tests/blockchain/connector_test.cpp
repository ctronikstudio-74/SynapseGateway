#include "gateway/blockchain/BlockchainConnector.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/blockchain/rpc_client.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

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
    config.usdc_contract = "0x00000000000000000000000000000000000000aa";
    config.usdc_decimals = 6;
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

std::string topicWord(const std::string& address) {
    const std::string body = address.substr(2);
    return "0x" + std::string(64 - body.size(), '0') + body;
}

TEST(BlockchainConnectorTest, ParsesFinalizedTransferAndSelectsCheapestHealthyNetwork) {
    auto rpc = std::make_shared<ScriptedRpc>();
    gateway::blockchain::BlockchainConnector connector(
        {networkConfig(gateway::core::Network::BASE, true), networkConfig(gateway::core::Network::ARBITRUM, true)},
        "seed", rpc);
    const std::string treasury = connector.paymentWallet();
    const std::string agent = "0x2222222222222222222222222222222222222222";
    rpc->handler = [treasury, agent](const std::string& method, const nlohmann::json&) {
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
            const std::string transfer = "0x" + topic;
            return nlohmann::json{
                {"status", "0x1"},
                {"blockNumber", "0x10"},
                {"logs",
                 nlohmann::json::array(
                     {{{"address", "0x00000000000000000000000000000000000000bb"},
                       {"topics", {transfer, topicWord(agent), topicWord(treasury)}},
                       {"data", "0xffffffff"}},
                      {{"address", "0x00000000000000000000000000000000000000aa"},
                       {"topics", {transfer, topicWord("0x3333333333333333333333333333333333333333"), topicWord(treasury)}},
                       {"data", "0x2710"}},
                      {{"address", "0x00000000000000000000000000000000000000aa"},
                       {"topics", {transfer, topicWord(agent), topicWord(treasury)}},
                       {"data", "0x3e8"}}})}};
        }
        if (method == "eth_getBlockByNumber") {
            return nlohmann::json{{"timestamp", "0x65"}};
        }
        return nlohmann::json("0x1");
    };

    connector.refreshGasFees();
    connector.refreshHealth();
    const auto selected = connector.selectOptimalNetwork();
    ASSERT_TRUE(selected);
    EXPECT_TRUE(connector.isNetworkHealthy(*selected));

    const auto lookup = connector.getPaymentTransfer("0xabc", gateway::core::Network::BASE, agent);
    ASSERT_EQ(lookup.status, gateway::blockchain::TxStatus::Found);
    ASSERT_TRUE(lookup.transaction);
    EXPECT_EQ(lookup.transaction->value_wei, "1000");
    EXPECT_EQ(lookup.transaction->to_address, treasury);
    EXPECT_EQ(lookup.transaction->from_address, agent);
    EXPECT_EQ(lookup.transaction->token_contract, "0x00000000000000000000000000000000000000aa");
    EXPECT_TRUE(lookup.transaction->is_finalized);
    EXPECT_EQ(lookup.transaction->timestamp, 0x65u);
}

TEST(BlockchainConnectorTest, AcceptsErc1271MagicValue) {
    auto rpc = std::make_shared<ScriptedRpc>();
    rpc->handler = [](const std::string& method, const nlohmann::json&) {
        if (method == "eth_call") {
            return nlohmann::json("0x1626ba7e00000000000000000000000000000000000000000000000000000000");
        }
        return nlohmann::json("0x1");
    };
    gateway::blockchain::BlockchainConnector connector({networkConfig(gateway::core::Network::BASE, true)}, "seed", rpc);
    EXPECT_TRUE(connector.isValidContractSignature("0x2222222222222222222222222222222222222222",
                                                   "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                                   "0x1234", gateway::core::Network::BASE));
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

TEST(BlockchainConnectorTest, BroadcastsAnEip1559Authorization) {
    auto rpc = std::make_shared<ScriptedRpc>();
    std::string raw;
    rpc->handler = [&raw](const std::string& method, const nlohmann::json& params) {
        if (method == "eth_getTransactionCount") {
            return nlohmann::json("0x0");
        }
        if (method == "eth_gasPrice") {
            return nlohmann::json("0x1");
        }
        if (method == "eth_sendRawTransaction") {
            raw = params.at(0).get<std::string>();
            return nlohmann::json("0x" + std::string(64, 'c'));
        }
        return nlohmann::json(nullptr);
    };
    const std::string key = "0x" + std::string(63, '0') + "1";
    gateway::blockchain::BlockchainConnector connector({networkConfig(gateway::core::Network::BASE, true)}, "seed", rpc, key);
    gateway::blockchain::TransferAuthorization authorization;
    authorization.from = "0x2222222222222222222222222222222222222222";
    authorization.to = connector.paymentWallet();
    authorization.value_units = "1000";
    authorization.valid_before = 4'000'000'000ULL;
    authorization.nonce_hex = "0x" + std::string(64, 'b');
    authorization.signature_hex = "0x" + std::string(130, '1');
    const auto broadcast = connector.submitTransferAuthorization(gateway::core::Network::BASE, authorization);
    EXPECT_EQ(broadcast.status, gateway::blockchain::Broadcast::Status::Submitted);
    EXPECT_EQ(broadcast.hash, "0x" + std::string(64, 'c'));
    EXPECT_EQ(raw.rfind("0x02", 0), 0U);
}
