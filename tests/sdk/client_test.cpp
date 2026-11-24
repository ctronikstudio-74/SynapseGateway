#include "synapse_gateway/client.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

namespace {

class ScriptTransport final : public synapse::gateway::Transport {
public:
    std::vector<synapse::gateway::HttpResponse> script;
    std::vector<std::string> methods;
    std::vector<std::string> urls;
    std::vector<std::string> bodies;
    std::vector<std::vector<std::pair<std::string, std::string>>> headers;

    synapse::gateway::HttpResponse exchange(const std::string& method,
                                            const std::string& url,
                                            const std::string& body,
                                            const std::vector<std::pair<std::string, std::string>>& request_headers) override {
        methods.push_back(method);
        urls.push_back(url);
        bodies.push_back(body);
        headers.push_back(request_headers);
        if (script.empty()) {
            throw std::runtime_error("scripted transport has no response");
        }
        synapse::gateway::HttpResponse response = std::move(script.front());
        script.erase(script.begin());
        return response;
    }
};

const char* kKey = "0000000000000000000000000000000000000000000000000000000000000001";
const char* kWallet = "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf";
const char* kCollection = "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5";

std::string headerValue(const std::vector<std::pair<std::string, std::string>>& headers, const std::string& name) {
    for (const auto& [key, value] : headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

}  // namespace

TEST(SdkClient, HealthAndCatalogSkipTheSignature) {
    auto transport = std::make_shared<ScriptTransport>();
    transport->script.push_back({200, R"({"status":"ok"})", {}});
    transport->script.push_back({200, R"({"protocol":"mcp","tools":[]})", {}});
    synapse::gateway::Client client("http://127.0.0.1:8080", kKey, kCollection, transport);

    EXPECT_EQ(client.health().status, 200);
    EXPECT_EQ(client.catalog().status, 200);
    EXPECT_EQ(transport->urls[0], "http://127.0.0.1:8080/health");
    EXPECT_EQ(transport->urls[1], "http://127.0.0.1:8080/mcp");
    EXPECT_TRUE(headerValue(transport->headers[0], "X-Agent-Signature").empty());
}

TEST(SdkClient, PaidRetryResignsTheIssuedQuote) {
    auto transport = std::make_shared<ScriptTransport>();
    transport->script.push_back(
        {402,
         R"({"error":"payment required","price_usdc":0.001,"payment_wallet":"0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5","network":"base","transaction_reference":"txref-1","expires_at_unix":1700000000,"payment_header":"X-Payment-Tx","quote_header":"X-Payment-Reference"})",
         {}});
    transport->script.push_back({200, R"({"ok":true})", {{"X-Payment-Amount", "0.001"}, {"X-Transaction-Hash", "0xabc"}}});
    synapse::gateway::Client client("http://127.0.0.1:8080/", kKey, kCollection, transport, 8453);
    EXPECT_EQ(client.wallet(), kWallet);

    const auto paid = client.invoke("GET", "/api/v1/chat", "", [](const synapse::gateway::Quote& quote) {
        EXPECT_EQ(quote.transaction_reference, "txref-1");
        EXPECT_EQ(quote.expires_at_unix, 1700000000u);
        return synapse::gateway::Payment{"0xabc", ""};
    });

    EXPECT_EQ(paid.status, 200);
    EXPECT_EQ(paid.header("X-Transaction-Hash"), "0xabc");
    EXPECT_EQ(transport->methods.size(), 2u);
    const std::string first = headerValue(transport->headers[0], "X-Agent-Signature");
    const std::string second = headerValue(transport->headers[1], "X-Agent-Signature");
    EXPECT_EQ(first.size(), 132u);
    EXPECT_EQ(second.size(), 132u);
    EXPECT_NE(first, second);
    EXPECT_EQ(headerValue(transport->headers[0], "X-Payment-Reference"), "");
    EXPECT_EQ(headerValue(transport->headers[1], "X-Payment-Reference"), "txref-1");
    EXPECT_EQ(headerValue(transport->headers[1], "X-Payment-Tx"), "0xabc");
    EXPECT_EQ(headerValue(transport->headers[1], "X-Agent-Wallet"), kWallet);
}

TEST(SdkClient, CreditsRefundsAndEdgeDecisionUseTheAgentRoutes) {
    auto transport = std::make_shared<ScriptTransport>();
    transport->script.push_back({200, R"({"credited":true})", {}});
    transport->script.push_back({200, R"({"refunded":true})", {}});
    transport->script.push_back({200, R"({"allowed":true})", {{"X-Agent-Wallet", kWallet}}});
    synapse::gateway::Client client("http://127.0.0.1:8080", kKey, kCollection, transport);

    EXPECT_EQ(client.deposit("0xdeposit").status, 200);
    EXPECT_EQ(transport->urls[0], "http://127.0.0.1:8080/v1/credits");
    EXPECT_NE(transport->bodies[0].find("0xdeposit"), std::string::npos);

    EXPECT_EQ(client.refund("0xpaid", kWallet, 0.0001).status, 200);
    EXPECT_EQ(transport->urls[1], "http://127.0.0.1:8080/v1/refunds");
    EXPECT_NE(transport->bodies[1].find("gas_fee_usdc"), std::string::npos);

    const auto decision = client.decide("GET", "/api/v1/chat", "", "traefik");
    EXPECT_EQ(decision.status, 200);
    EXPECT_EQ(transport->urls[2], "http://127.0.0.1:8080/v1/auth");
    EXPECT_EQ(headerValue(transport->headers[2], "X-Forwarded-Method"), "GET");
    EXPECT_EQ(headerValue(transport->headers[2], "X-Forwarded-Uri"), "/api/v1/chat");
    EXPECT_EQ(headerValue(transport->headers[2], "X-Agent-Signature").size(), 132u);
}

TEST(SdkClient, McpInitializePostsJsonRpc) {
    auto transport = std::make_shared<ScriptTransport>();
    transport->script.push_back({200, R"({"jsonrpc":"2.0","result":{"protocolVersion":"2024-11-05"}})", {}});
    synapse::gateway::Client client("http://127.0.0.1:8080", kKey, kCollection, transport);
    EXPECT_EQ(client.mcp("initialize").status, 200);
    EXPECT_EQ(transport->methods[0], "POST");
    EXPECT_NE(transport->bodies[0].find("initialize"), std::string::npos);
    EXPECT_NE(transport->bodies[0].find("jsonrpc"), std::string::npos);
}
