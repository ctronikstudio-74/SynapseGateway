#include "gateway/core/header_policy.h"

#include "../support/property.h"

#include <gtest/gtest.h>

#include <cctype>
#include <random>
#include <string>
#include <vector>

namespace {

std::string randomToken(std::mt19937& rng, int length) {
    static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    std::uniform_int_distribution<int> pick(0, static_cast<int>(sizeof(kAlphabet) - 2));
    std::string value;
    value.resize(static_cast<std::size_t>(length));
    for (char& character : value) {
        character = kAlphabet[pick(rng)];
    }
    return value;
}

}  // namespace

TEST(HeaderPolicyProperty, PreservesNonPaymentHeaders) {
    gateway::test::checkProperty("header filtering", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> count(1, 8);
        std::uniform_int_distribution<int> kind(0, 2);
        const char* payment_names[] = {"X-Payment-Tx", "X-402-Payment", "X-Agent-Signature", "X-Transaction-Hash"};
        std::vector<gateway::core::HttpHeader> incoming;
        std::vector<std::string> expected;
        for (int index = 0; index < count(rng); ++index) {
            if (kind(rng) == 0) {
                incoming.push_back({payment_names[static_cast<std::size_t>(index) % 4], "0xabc\r\nInjected: 1"});
            } else {
                const std::string name = "X-Safe-" + std::to_string(index);
                incoming.push_back({name, "value-" + std::to_string(index) + "\r\nBcc: evil"});
                expected.push_back(name);
            }
        }
        const auto forwarded = gateway::core::headersForBackend(incoming);
        ASSERT_EQ(forwarded.size(), expected.size());
        for (std::size_t index = 0; index < forwarded.size(); ++index) {
            EXPECT_EQ(forwarded[index].name, expected[index]);
            EXPECT_FALSE(gateway::core::isPaymentHeader(forwarded[index].name));
            EXPECT_EQ(forwarded[index].value.find('\r'), std::string::npos);
            EXPECT_EQ(forwarded[index].value.find('\n'), std::string::npos);
            EXPECT_NE(forwarded[index].value.find("value-"), std::string::npos);
        }
    });
}

TEST(HeaderPolicyProperty, StripsInjectionCharacters) {
    gateway::test::checkProperty("header sanitization", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> controls(0, 31);
        std::string dirty = "ok-" + randomToken(rng, 12);
        dirty.push_back('\r');
        dirty.push_back('\n');
        dirty += "X-Injected: ";
        dirty.push_back(static_cast<char>(controls(rng)));
        dirty += randomToken(rng, 6);
        const auto clean = gateway::core::sanitizeHeaderValue(dirty);
        for (const unsigned char character : clean) {
            EXPECT_GE(character, 0x20);
            EXPECT_NE(character, 0x7f);
        }
        EXPECT_EQ(clean.find('\n'), std::string::npos);
        EXPECT_EQ(clean.find('\r'), std::string::npos);
        std::vector<gateway::core::HttpHeader> incoming = {{"X-Request-Id", dirty},
                                                           {"Bad Name", "nope"},
                                                           {"X-Payment-Tx", "hash"}};
        const auto forwarded = gateway::core::headersForBackend(incoming);
        ASSERT_EQ(forwarded.size(), 1u);
        EXPECT_EQ(forwarded[0].name, "X-Request-Id");
        EXPECT_EQ(forwarded[0].value, clean);
    });
}

TEST(HeaderPolicyTest, RequiresASignatureToken) {
    EXPECT_FALSE(gateway::core::hasAgentSignature({{"X-Agent-Wallet", "0xabc"}}));
    EXPECT_FALSE(gateway::core::hasAgentSignature({{"X-Agent-Signature", "short"}}));
    EXPECT_FALSE(gateway::core::hasAgentSignature({{"X-Agent-Signature", std::string(64, 'a') + "\nbad"}}));
    EXPECT_TRUE(gateway::core::hasAgentSignature({{"X-Agent-Signature", std::string(64, 'a')}}));
}
