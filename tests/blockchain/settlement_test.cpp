#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/request_signature.h"
#include "gateway/blockchain/rlp.h"
#include "gateway/blockchain/settlement.h"

#include <gtest/gtest.h>

#include <string>

TEST(SettlementTest, RlpEncodesTheClassicListVector) {
    const auto dog = gateway::blockchain::rlpBytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>("dog"), 3));
    const auto cat = gateway::blockchain::rlpBytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>("cat"), 3));
    const auto encoded = gateway::blockchain::rlpList({dog, cat});
    EXPECT_EQ(gateway::blockchain::toHex(encoded), "c883646f6783636174");
    EXPECT_EQ(gateway::blockchain::rlpList({}).size(), 1U);
    EXPECT_EQ(gateway::blockchain::rlpList({})[0], 0xc0);
    EXPECT_EQ(gateway::blockchain::rlpUint(0).size(), 1U);
    EXPECT_EQ(gateway::blockchain::rlpUint(0)[0], 0x80);
}

TEST(SettlementTest, AuthorizationDigestRecoversTheSigner) {
    const std::string key = "0x" + std::string(63, '0') + "1";
    const auto agent = gateway::blockchain::addressFromPrivateKey(key);
    ASSERT_TRUE(agent);
    gateway::blockchain::TransferAuthorization authorization;
    authorization.from = *agent;
    authorization.to = "0x1111111111111111111111111111111111111111";
    authorization.value_units = "1000";
    authorization.valid_after = 0;
    authorization.valid_before = 4'000'000'000ULL;
    authorization.nonce_hex = "0x" + std::string(64, 'b');
    const auto digest = gateway::blockchain::transferAuthorizationDigest(
        authorization, "USD Coin", "2", 8453, "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const auto signature = gateway::blockchain::signDigest(key, digest);
    ASSERT_TRUE(signature);
    const auto recovered = gateway::blockchain::recoverSigner(digest, "0x" + gateway::blockchain::toHex(*signature));
    ASSERT_TRUE(recovered);
    EXPECT_EQ(gateway::blockchain::normalizeAddress(*recovered), gateway::blockchain::normalizeAddress(*agent));
    const auto calldata = gateway::blockchain::transferAuthorizationCalldata(authorization);
    ASSERT_GE(calldata.size(), 4U);
    EXPECT_NE(calldata[0], 0);
}
