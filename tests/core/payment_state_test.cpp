#include "gateway/core/ConfigurationManager.h"
#include "gateway/core/payment_state.h"

#include "../support/property.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <string>

namespace {

void setEnvironment(const char* name, const char* value) {
#ifdef _MSC_VER
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}  // namespace

TEST(PaymentStateProperty, RoundTripsPendingVerification) {
    gateway::test::checkProperty("state persistence", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> count(0, 6);
        std::uniform_int_distribution<int> price(0, 1000);
        std::uniform_int_distribution<int> stamp(1, 2'000'000'000);
        gateway::core::PersistedGatewayState state;
        const int items = count(rng);
        for (int index = 0; index < items; ++index) {
            gateway::core::PendingVerification pending;
            pending.tx_hash = "0x" + std::to_string(index) + "-" + std::to_string(stamp(rng));
            pending.wallet_address = "0x2222222222222222222222222222222222222222";
            pending.endpoint_path = "/api/" + std::to_string(index);
            pending.price_usdc = static_cast<double>(price(rng));
            pending.status = index % 2 == 0 ? "pending" : "verified";
            pending.updated_at_unix = static_cast<std::uint64_t>(stamp(rng));
            state.pending.push_back(pending);
            state.seen_transactions.push_back(pending.tx_hash);
        }
        const auto encoded = gateway::core::serializeGatewayState(state);
        const auto decoded = gateway::core::parseGatewayState(encoded);
        ASSERT_EQ(decoded.pending.size(), state.pending.size());
        for (std::size_t index = 0; index < state.pending.size(); ++index) {
            EXPECT_EQ(decoded.pending[index], state.pending[index]);
        }
        EXPECT_EQ(decoded.seen_transactions, state.seen_transactions);
    });
}

TEST(ConfigurationEnvironment, ExpandsPlaceholdersAndKeepsMissingNames) {
    setEnvironment("AGENTGW_EXPAND_TEST", "expanded-value");
    const auto expanded = gateway::core::ConfigurationManager::expandEnvironmentPlaceholders(
        "payment_wallet_seed = \"${AGENTGW_EXPAND_TEST}\"");
    EXPECT_NE(expanded.find("expanded-value"), std::string::npos);
    const auto missing = gateway::core::ConfigurationManager::expandEnvironmentPlaceholders("${AGENTGW_EXPAND_MISSING_ZZZ}");
    EXPECT_EQ(missing, "${AGENTGW_EXPAND_MISSING_ZZZ}");
}
