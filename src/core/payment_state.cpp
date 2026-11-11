#include "gateway/core/payment_state.h"

#include "gateway/core/Configuration.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace gateway::core {

std::string serializeGatewayState(const PersistedGatewayState& state) {
    nlohmann::json refunds = nlohmann::json::array();
    for (const auto& refund : state.refunds) {
        refunds.push_back({{"tx_hash", refund.tx_hash},
                           {"wallet_address", refund.wallet_address},
                           {"amount_usdc", refund.amount_usdc},
                           {"gas_fee_usdc", refund.gas_fee_usdc},
                           {"refund_amount_usdc", refund.refund_amount_usdc},
                           {"network", NetworkToString(refund.network)},
                           {"reason", refund.reason},
                           {"refund_requested_at", refund.refund_requested_at},
                           {"refund_processed", refund.refund_processed}});
    }
    nlohmann::json pending = nlohmann::json::array();
    for (const auto& item : state.pending) {
        pending.push_back({{"tx_hash", item.tx_hash},
                           {"wallet_address", item.wallet_address},
                           {"endpoint_path", item.endpoint_path},
                           {"price_usdc", item.price_usdc},
                           {"status", item.status},
                           {"updated_at_unix", item.updated_at_unix}});
    }
    nlohmann::json verified = nlohmann::json::array();
    for (const auto& item : state.verified_until_unix) {
        verified.push_back({{"tx_hash", item.first}, {"expires_at_unix", item.second}});
    }
    return nlohmann::json{{"refunds", refunds},
                          {"pending", pending},
                          {"seen_transactions", state.seen_transactions},
                          {"verified_until", verified}}
        .dump(2);
}

PersistedGatewayState parseGatewayState(std::string_view json_text) {
    PersistedGatewayState state;
    const nlohmann::json parsed = nlohmann::json::parse(json_text, nullptr, false);
    if (!parsed.is_object()) {
        return state;
    }
    if (parsed.contains("refunds") && parsed["refunds"].is_array()) {
        for (const auto& item : parsed["refunds"]) {
            gateway::models::RefundableTransaction record;
            record.tx_hash = item.value("tx_hash", "");
            record.wallet_address = item.value("wallet_address", "");
            record.amount_usdc = item.value("amount_usdc", 0.0);
            record.gas_fee_usdc = item.value("gas_fee_usdc", 0.0);
            record.refund_amount_usdc = item.value("refund_amount_usdc", 0.0);
            record.reason = item.value("reason", "");
            record.refund_requested_at = item.value("refund_requested_at", static_cast<std::uint64_t>(0));
            record.refund_processed = item.value("refund_processed", false);
            try {
                record.network = NetworkFromString(item.value("network", "base"));
            } catch (const std::exception&) {
                record.network = Network::BASE;
            }
            state.refunds.push_back(std::move(record));
        }
    }
    if (parsed.contains("pending") && parsed["pending"].is_array()) {
        for (const auto& item : parsed["pending"]) {
            PendingVerification record;
            record.tx_hash = item.value("tx_hash", "");
            record.wallet_address = item.value("wallet_address", "");
            record.endpoint_path = item.value("endpoint_path", "");
            record.price_usdc = item.value("price_usdc", 0.0);
            record.status = item.value("status", "");
            record.updated_at_unix = item.value("updated_at_unix", static_cast<std::uint64_t>(0));
            state.pending.push_back(std::move(record));
        }
    }
    if (parsed.contains("seen_transactions") && parsed["seen_transactions"].is_array()) {
        for (const auto& item : parsed["seen_transactions"]) {
            if (item.is_string()) {
                state.seen_transactions.push_back(item.get<std::string>());
            }
        }
    }
    if (parsed.contains("verified_until") && parsed["verified_until"].is_array()) {
        for (const auto& item : parsed["verified_until"]) {
            state.verified_until_unix.emplace_back(item.value("tx_hash", ""),
                                                   item.value("expires_at_unix", static_cast<std::uint64_t>(0)));
        }
    }
    return state;
}

}  // namespace gateway::core
