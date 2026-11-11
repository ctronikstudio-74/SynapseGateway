#include "gateway/blockchain/abi_decoder.h"
#include "gateway/blockchain/hex.h"

#include "../support/property.h"

#include <gtest/gtest.h>

using gateway::blockchain::AbiDecoder;
using gateway::blockchain::hexToDecimal;
using gateway::blockchain::toHex;

TEST(AbiDecoderTest, BalanceOfSelectorAndHexDecimal) {
    EXPECT_EQ(toHex(AbiDecoder::functionSelector("balanceOf(address)")), "70a08231");
    EXPECT_EQ(hexToDecimal("0xff"), "255");
    EXPECT_EQ(hexToDecimal("0x10"), "16");
    EXPECT_EQ(hexToDecimal("0x0"), "0");
}

TEST(AbiDecoderTest, IdentityAndReputationRoundTrip) {
    gateway::test::checkProperty("abi metadata", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> length(0, 40);
        std::uniform_int_distribution<int> alphabet(0, 25);
        std::string identity(static_cast<std::size_t>(length(rng)), 'a');
        for (char& c : identity) {
            c = static_cast<char>('a' + alphabet(rng));
        }
        std::uniform_int_distribution<std::uint64_t> stamp(1, 2'000'000'000ULL);
        const auto registered = stamp(rng);
        const auto encoded = AbiDecoder::encodeIdentityReturn(identity, registered);
        const auto decoded = AbiDecoder::decodeIdentityData(encoded, "9", "0xabc");
        EXPECT_EQ(decoded.identity_type, identity);
        EXPECT_EQ(decoded.registration_timestamp, registered);
        EXPECT_EQ(decoded.token_id, "9");

        const auto reputation = AbiDecoder::encodeReputationReturn(9000, 40, 8000, 12, registered);
        const auto parsed = AbiDecoder::decodeReputation(reputation, "0xabc");
        EXPECT_NEAR(parsed.success_rate, 0.9, 1e-12);
        EXPECT_DOUBLE_EQ(parsed.avg_response_time_ms, 40.0);
        EXPECT_NEAR(parsed.feedback_score, 0.8, 1e-12);
        EXPECT_EQ(parsed.total_requests, 12u);
        EXPECT_EQ(parsed.last_updated, registered);
    });
}
