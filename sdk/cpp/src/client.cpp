#include "synapse_gateway/client.hpp"

#include "gateway/blockchain/request_signature.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace synapse::gateway {
namespace {

std::string lower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string joinUrl(const std::string& base, const std::string& path) {
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        return path;
    }
    std::string suffix = path;
    if (suffix.empty() || suffix.front() != '/') {
        suffix.insert(suffix.begin(), '/');
    }
    return trimSlash(base) + suffix;
}

bool sameAddress(const std::string& left, const std::string& right) {
    return lower(left) == lower(right);
}

std::optional<Quote> quoteFrom(int status, const std::string& body) {
    if (status != 402) {
        return std::nullopt;
    }
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (!parsed.is_object()) {
        return std::nullopt;
    }
    Quote quote;
    quote.price_usdc = parsed.value("price_usdc", 0.0);
    quote.balance_usdc = parsed.value("balance_usdc", 0.0);
    quote.payment_wallet = parsed.value("payment_wallet", "");
    quote.network = parsed.value("network", "");
    quote.transaction_reference = parsed.value("transaction_reference", "");
    quote.expires_at_unix = parsed.value("expires_at_unix", static_cast<std::uint64_t>(0));
    quote.top_up = parsed.value("top_up", "");
    return quote;
}

}  // namespace

std::string CallResult::header(const std::string& name) const {
    const std::string wanted = lower(name);
    for (const auto& [key, value] : headers) {
        if (lower(key) == wanted) {
            return value;
        }
    }
    return {};
}

std::uint64_t chainIdForNetwork(const std::string& network) {
    const std::string name = lower(network);
    if (name == "ethereum_mainnet" || name == "ethereum") {
        return 1;
    }
    if (name == "base") {
        return 8453;
    }
    if (name == "arbitrum") {
        return 42161;
    }
    if (name == "polygon") {
        return 137;
    }
    if (name == "bnb_chain" || name == "bnb") {
        return 56;
    }
    return 0;
}

Client::Client(std::string base_url,
               std::string private_key_hex,
               std::string verifying_contract,
               std::shared_ptr<Transport> transport,
               std::uint64_t chain_id)
    : base_url_(std::move(base_url))
    , private_key_hex_(std::move(private_key_hex))
    , verifying_contract_(std::move(verifying_contract))
    , transport_(std::move(transport))
    , chain_id_(chain_id) {
    if (!transport_ || base_url_.empty() || verifying_contract_.empty() || chain_id_ == 0) {
        throw std::invalid_argument("SynapseGateway client requires a transport, base URL, collection address, and chain id");
    }
    const auto derived = gateway::blockchain::addressFromPrivateKey(private_key_hex_);
    if (!derived) {
        throw std::invalid_argument("private key does not derive an agent wallet");
    }
    wallet_ = *derived;
}

const std::string& Client::wallet() const {
    return wallet_;
}

const std::string& Client::verifyingContract() const {
    return verifying_contract_;
}

CallResult Client::exchange(const std::string& method,
                            const std::string& path,
                            const std::string& body,
                            const std::vector<std::pair<std::string, std::string>>& headers,
                            bool signed_call,
                            const CallOptions& options) const {
    std::vector<std::pair<std::string, std::string>> outbound = headers;
    if (signed_call) {
        gateway::blockchain::AgentRequest request;
        request.method = method;
        request.path = path;
        request.body = body;
        request.wallet = wallet_;
        request.quote_reference = options.reference;
        request.expiry_unix = options.reference.empty() ? 0 : options.expiry_unix;
        request.chain_id = options.chain_id == 0 ? chain_id_ : options.chain_id;
        request.verifying_contract = verifying_contract_;
        const auto signature = gateway::blockchain::signAgentRequest(private_key_hex_, request);
        if (!signature) {
            throw std::runtime_error("agent request signature failed");
        }
        outbound.emplace_back("X-Agent-Signature", *signature);
        outbound.emplace_back("X-Agent-Wallet", wallet_);
        if (!options.tx_hash.empty()) {
            outbound.emplace_back("X-Payment-Tx", options.tx_hash);
        }
        if (!options.reference.empty()) {
            outbound.emplace_back("X-Payment-Reference", options.reference);
        }
        if (!options.authorization_json.empty()) {
            outbound.emplace_back("X-Payment-Authorization", options.authorization_json);
        }
        if (!options.proof.empty()) {
            outbound.emplace_back("X-Agent-Proof", options.proof);
        }
    }
    if (!body.empty()) {
        outbound.emplace_back("Content-Type", "application/json");
    }
    const HttpResponse response = transport_->exchange(method, joinUrl(base_url_, path), body, outbound);
    CallResult result;
    result.status = response.status;
    result.body = response.body;
    result.headers = response.headers;
    result.quote = quoteFrom(response.status, response.body);
    if (result.quote && !result.quote->payment_wallet.empty() &&
        !sameAddress(result.quote->payment_wallet, verifying_contract_)) {
        throw std::invalid_argument("quote collection address does not match the client verifying contract");
    }
    return result;
}

CallResult Client::health() const {
    return exchange("GET", "/health", "", {}, false, {});
}

CallResult Client::catalog() const {
    return exchange("GET", "/mcp", "", {}, false, {});
}

CallResult Client::mcp(const std::string& method, const std::string& params_json, const std::string& id) const {
    nlohmann::json params = nlohmann::json::parse(params_json.empty() ? "{}" : params_json, nullptr, false);
    if (!params.is_object() && !params.is_array()) {
        params = nlohmann::json::object();
    }
    nlohmann::json payload = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
    if (method == "notifications/initialized") {
        payload.erase("id");
    }
    return exchange("POST", "/mcp", payload.dump(), {}, false, {});
}

CallResult Client::call(const std::string& method, const std::string& path, const std::string& body, const CallOptions& options) const {
    return exchange(method, path, body, {}, true, options);
}

CallResult Client::invoke(const std::string& method,
                          const std::string& path,
                          const std::string& body,
                          const std::function<Payment(const Quote&)>& payer) const {
    CallResult first = call(method, path, body, {});
    if (first.status != 402 || !first.quote || first.quote->transaction_reference.empty() || !payer) {
        return first;
    }
    const Payment payment = payer(*first.quote);
    if (payment.tx_hash.empty() && payment.authorization_json.empty()) {
        return first;
    }
    const std::uint64_t chain = chainIdForNetwork(first.quote->network);
    CallOptions options;
    options.tx_hash = payment.tx_hash;
    options.authorization_json = payment.authorization_json;
    options.reference = first.quote->transaction_reference;
    options.expiry_unix = first.quote->expires_at_unix;
    options.chain_id = chain == 0 ? chain_id_ : chain;
    return call(method, path, body, options);
}

CallResult Client::deposit(const std::string& tx_hash) const {
    const nlohmann::json payload = {{"tx_hash", tx_hash}};
    return call("POST", "/v1/credits", payload.dump(), {});
}

CallResult Client::refund(const std::string& tx_hash, const std::string& wallet_address, double gas_fee_usdc) const {
    const nlohmann::json payload = {{"tx_hash", tx_hash}, {"wallet_address", wallet_address}, {"gas_fee_usdc", gas_fee_usdc}};
    return call("POST", "/v1/refunds", payload.dump(), {});
}

CallResult Client::decide(const std::string& original_method,
                          const std::string& original_path,
                          const std::string& body,
                          const std::string& proxy) const {
    CallOptions options;
    std::vector<std::pair<std::string, std::string>> headers;
    const std::string kind = lower(proxy);
    if (kind == "traefik") {
        headers.emplace_back("X-Forwarded-Method", original_method);
        headers.emplace_back("X-Forwarded-Uri", original_path);
    } else if (kind == "envoy") {
        headers.emplace_back("X-Original-Method", original_method);
        headers.emplace_back("X-Envoy-Original-Path", original_path);
    } else {
        headers.emplace_back("X-Original-Method", original_method);
        headers.emplace_back("X-Original-URI", original_path);
    }
    gateway::blockchain::AgentRequest request;
    request.method = original_method;
    request.path = original_path;
    request.body = body;
    request.wallet = wallet_;
    request.chain_id = chain_id_;
    request.verifying_contract = verifying_contract_;
    const auto signature = gateway::blockchain::signAgentRequest(private_key_hex_, request);
    if (!signature) {
        throw std::runtime_error("agent request signature failed");
    }
    headers.emplace_back("X-Agent-Signature", *signature);
    headers.emplace_back("X-Agent-Wallet", wallet_);
    return exchange("POST", "/v1/auth", body, headers, false, options);
}

}  // namespace synapse::gateway
