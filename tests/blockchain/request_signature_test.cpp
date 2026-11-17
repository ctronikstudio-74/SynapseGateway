#include "gateway/blockchain/request_signature.h"

#include <gtest/gtest.h>

namespace {

const std::string kPrivateKey = "0x0000000000000000000000000000000000000000000000000000000000000001";

}  // namespace

TEST(RequestSignatureTest, PrivateKeyOneRecoversTheSignedRequest) {
    const auto address = gateway::blockchain::addressFromPrivateKey(kPrivateKey);
    ASSERT_TRUE(address);
    EXPECT_EQ(*address, "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf");

    gateway::blockchain::AgentRequest request;
    request.method = "POST";
    request.path = "/api/paid";
    request.body = "{\"ping\":1}";
    request.wallet = *address;
    request.quote_reference = "txref-1";
    request.expiry_unix = 1'700'000'000;
    request.chain_id = gateway::blockchain::chainIdOf(gateway::core::Network::BASE);
    request.verifying_contract = "0x1111111111111111111111111111111111111111";

    const auto signature = gateway::blockchain::signAgentRequest(kPrivateKey, request);
    ASSERT_TRUE(signature);
    const auto digest = gateway::blockchain::agentRequestDigest(request);
    const auto signer = gateway::blockchain::recoverSigner(digest, *signature);
    ASSERT_TRUE(signer);
    EXPECT_EQ(*signer, *address);

    request.path = "/api/other";
    const auto rebound = gateway::blockchain::recoverSigner(gateway::blockchain::agentRequestDigest(request), *signature);
    ASSERT_TRUE(rebound);
    EXPECT_NE(*rebound, *address);
}
