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
                           {"refund_processed", refund.refund_processed},
                           {"refund_tx_hash", refund.refund_tx_hash}});
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
    for (const auto& item : state.verified_until) {
        verified.push_back({{"tx_hash", item.tx_hash},
                            {"expires_at_unix", item.expires_at_unix},
                            {"endpoint_path", item.endpoint_path},
                            {"wallet_address", item.wallet_address},
                            {"network", item.network},
                            {"reference", item.reference},
                            {"price_usdc", item.price_usdc}});
    }
    nlohmann::json credits = nlohmann::json::array();
    for (const auto& item : state.credits) {
        credits.push_back({{"wallet_address", item.wallet_address}, {"balance_usdc", item.balance_usdc}});
    }
    nlohmann::json authorizations = nlohmann::json::array();
    for (const auto& item : state.authorizations) {
        authorizations.push_back({{"nonce", item.nonce}, {"tx_hash", item.tx_hash}});
    }
    nlohmann::json quotes = nlohmann::json::array();
    for (const auto& item : state.quotes) {
        quotes.push_back({{"reference", item.reference},
                          {"endpoint_path", item.endpoint_path},
                          {"wallet_address", item.wallet_address},
                          {"network", item.network},
                          {"price_usdc", item.price_usdc},
                          {"expires_at_unix", item.expires_at_unix}});
    }
    return nlohmann::json{{"refunds", refunds},
                          {"pending", pending},
                          {"seen_transactions", state.seen_transactions},
                          {"verified_until", verified},
                          {"quotes", quotes},
                          {"credits", credits},
                          {"authorizations", authorizations}}
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
            record.refund_tx_hash = item.value("refund_tx_hash", "");
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
            VerifiedPaymentRecord record;
            record.tx_hash = item.value("tx_hash", "");
            record.expires_at_unix = item.value("expires_at_unix", static_cast<std::uint64_t>(0));
            record.endpoint_path = item.value("endpoint_path", "");
            record.wallet_address = item.value("wallet_address", "");
            record.network = item.value("network", "");
            record.reference = item.value("reference", "");
            record.price_usdc = item.value("price_usdc", 0.0);
            state.verified_until.push_back(std::move(record));
        }
    }
    if (parsed.contains("quotes") && parsed["quotes"].is_array()) {
        for (const auto& item : parsed["quotes"]) {
            IssuedQuote quote;
            quote.reference = item.value("reference", "");
            quote.endpoint_path = item.value("endpoint_path", "");
            quote.wallet_address = item.value("wallet_address", "");
            quote.network = item.value("network", "");
            quote.price_usdc = item.value("price_usdc", 0.0);
            quote.expires_at_unix = item.value("expires_at_unix", static_cast<std::uint64_t>(0));
            state.quotes.push_back(std::move(quote));
        }
    }
    if (parsed.contains("credits") && parsed["credits"].is_array()) {
        for (const auto& item : parsed["credits"]) {
            PrepaidBalance credit;
            credit.wallet_address = item.value("wallet_address", "");
            credit.balance_usdc = item.value("balance_usdc", 0.0);
            state.credits.push_back(std::move(credit));
        }
    }
    if (parsed.contains("authorizations") && parsed["authorizations"].is_array()) {
        for (const auto& item : parsed["authorizations"]) {
            SubmittedAuthorization authorization;
            authorization.nonce = item.value("nonce", "");
            authorization.tx_hash = item.value("tx_hash", "");
            state.authorizations.push_back(std::move(authorization));
        }
    }
    return state;
}

}  // namespace gateway::core
