#include "gateway/core/wallet_policy.h"

#include <gtest/gtest.h>

TEST(WalletPolicyTest, DenylistAndAllowlistCompareAddressesIgnoringCase) {
    const std::vector<std::string> listed = {"0xAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
    EXPECT_TRUE(gateway::core::walletListed(listed, "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    EXPECT_FALSE(gateway::core::walletListed(listed, "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    EXPECT_FALSE(gateway::core::walletListed({}, "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST(WalletPolicyTest, MinimumNftAgeIsDisabledAtZero) {
    EXPECT_TRUE(gateway::core::nftMeetsMinimumAge(10, 0, 11));
    EXPECT_FALSE(gateway::core::nftMeetsMinimumAge(0, 86'400, 100'000));
    EXPECT_FALSE(gateway::core::nftMeetsMinimumAge(1'000, 100, 1'050));
    EXPECT_TRUE(gateway::core::nftMeetsMinimumAge(1'000, 100, 1'100));
}
