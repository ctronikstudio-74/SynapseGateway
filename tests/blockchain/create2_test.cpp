#include "gateway/blockchain/create2.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/core/validation_rules.h"

#include "../support/property.h"

#include <gtest/gtest.h>

using gateway::blockchain::generateDeterministicWallet;
using gateway::blockchain::keccak256;
using gateway::blockchain::toHex;
using gateway::core::Network;
using gateway::core::isValidAddress;

TEST(KeccakTest, EmptyStringMatchesEthereumVector) {
    const auto digest = keccak256("");
    EXPECT_EQ(toHex(digest), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

TEST(Create2Wallet, SameSeedIsStableAcrossNetworks) {
    const Network networks[] = {Network::ETHEREUM_MAINNET, Network::BASE, Network::ARBITRUM, Network::POLYGON,
                                Network::BNB_CHAIN};
    gateway::test::checkProperty("create2", [&](std::mt19937& rng) {
        std::uniform_int_distribution<int> alphabet(0, 15);
        std::string seed = "seed-";
        for (int i = 0; i < 16; ++i) {
            seed.push_back("0123456789abcdef"[alphabet(rng)]);
        }
        const std::string first = generateDeterministicWallet(seed, networks[0]);
        EXPECT_TRUE(isValidAddress(first));
        for (const Network network : networks) {
            EXPECT_EQ(generateDeterministicWallet(seed, network), first);
        }
        EXPECT_NE(generateDeterministicWallet(seed + "x", networks[0]), first);
    });
}
