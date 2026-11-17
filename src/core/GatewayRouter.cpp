#include "gateway/core/GatewayRouter.h"

#include "gateway/blockchain/BlockchainConnector.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/models.h"
#include "gateway/blockchain/request_signature.h"
#include "gateway/core/wallet_policy.h"
#include "gateway/monitoring/MetricsCollector.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gateway::core {
namespace {

bool ownedByGateway(std::string_view name) {
    std::string lowered(name);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered == "x-agent-wallet" || lowered == "x-agent-identity-token" || lowered == "x-agent-identity-type" ||
           lowered == "x-agent-reputation" || lowered == "x-agent-network" || lowered == "x-agent-requests";
}

std::string pathOnly(const std::string& target) {
    const auto query = target.find('?');
    return query == std::string::npos ? target : target.substr(0, query);
}

std::string statusText(PaymentResponse::Status status) {
    switch (status) {
        case PaymentResponse::Status::NOT_PAID:
            return "payment required";
        case PaymentResponse::Status::VERIFICATION_PENDING:
            return "verification pending";
        case PaymentResponse::Status::VERIFIED:
            return "verified";
        case PaymentResponse::Status::INSUFFICIENT_AMOUNT:
            return "insufficient amount";
        case PaymentResponse::Status::INVALID_TRANSACTION:
            return "invalid transaction";
        case PaymentResponse::Status::REPLAY_DETECTED:
            return "replay detected";
    }
    return "payment required";
}

class InFlightGuard {
public:
    explicit InFlightGuard(std::atomic<std::uint64_t>& counter) : counter_(counter) { counter_.fetch_add(1); }
    ~InFlightGuard() { counter_.fetch_sub(1); }
    InFlightGuard(const InFlightGuard&) = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;

private:
    std::atomic<std::uint64_t>& counter_;
};

std::optional<std::string> forwardedValue(const std::vector<HttpHeader>& headers, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (const auto value = headerValue(headers, name)) {
            if (!value->empty()) {
                return value;
            }
        }
    }
    return std::nullopt;
}

std::string uriPath(std::string value) {
    if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) {
        const auto scheme = value.find("://");
        const auto slash = value.find('/', scheme + 3);
        value = slash == std::string::npos ? "/" : value.substr(slash);
    }
    if (value.empty() || value.front() != '/') {
        value.insert(value.begin(), '/');
    }
    return value;
}

std::optional<ForwardRequest> requestFromEdge(const ForwardRequest& incoming) {
    const auto method = forwardedValue(incoming.headers, {"X-Original-Method", "X-Forwarded-Method"});
    const auto uri = forwardedValue(incoming.headers, {"X-Original-URI", "X-Forwarded-Uri", "X-Envoy-Original-Path"});
    if (!method || !uri) {
        return std::nullopt;
    }
    ForwardRequest rewritten = incoming;
    rewritten.method = *method;
    rewritten.target = uriPath(*uri);
    if (pathOnly(rewritten.target) == "/v1/auth" || pathOnly(rewritten.target) == "/mcp") {
        return std::nullopt;
    }
    rewritten.decision_only = true;
    return rewritten;
}

std::string toolName(std::string path) {
    std::string name;
    for (unsigned char character : path) {
        if (std::isalnum(character) != 0) {
            name.push_back(static_cast<char>(std::tolower(character)));
        } else if (!name.empty() && name.back() != '_') {
            name.push_back('_');
        }
    }
    while (!name.empty() && name.back() == '_') {
        name.pop_back();
    }
    return name.empty() ? "endpoint" : name;
}

nlohmann::json toolCatalog(const Configuration& config, const std::string& wallet) {
    nlohmann::json tools = nlohmann::json::array();
    for (const EndpointConfig& endpoint : config.endpoints) {
        nlohmann::json proofs = nlohmann::json::array();
        for (const ProofType proof : endpoint.required_proofs) {
            proofs.push_back(ProofTypeToString(proof));
        }
        tools.push_back({{"name", toolName(endpoint.path_pattern)},
                         {"path", endpoint.path_pattern},
                         {"description", "Paid SynapseGateway route " + endpoint.path_pattern},
                         {"price_usdc", endpoint.price_usdc},
                         {"trust_threshold", endpoint.trust_threshold},
                         {"payment_wallet", wallet},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"body", {{"type", "string"}}}}}}},
                         {"required_proofs", std::move(proofs)}});
    }
    return tools;
}

GatewayResponse mcpResponse(const Configuration& config, const std::string& wallet, const ForwardRequest& request) {
    const nlohmann::json tools = toolCatalog(config, wallet);
    GatewayResponse response;
    response.content_type = "application/json";
    response.status = 200;
    if (request.method == "GET") {
        response.body = nlohmann::json{{"protocol", "mcp"}, {"service", "SynapseGateway"}, {"payment_wallet", wallet}, {"tools", tools}}.dump();
        return response;
    }
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (!body.is_object() || !body.contains("method")) {
        response.status = 400;
        response.body = R"({"jsonrpc":"2.0","error":{"code":-32600,"message":"invalid request"}})";
        return response;
    }
    const std::string method = body.value("method", "");
    nlohmann::json result;
    if (method == "initialize") {
        result = {{"protocolVersion", "2024-11-05"},
                  {"capabilities", {{"tools", nlohmann::json::object()}}},
                  {"serverInfo", {{"name", "SynapseGateway"}, {"version", "0.3.0"}}}};
    } else if (method == "tools/list") {
        result = {{"tools", tools}};
    } else if (method == "notifications/initialized") {
        response.body = "{}";
        return response;
    } else {
        response.body = nlohmann::json{{"jsonrpc", "2.0"},
                                       {"id", body.contains("id") ? body["id"] : nullptr},
                                       {"error", {{"code", -32601}, {"message", "method not found"}}}}
                           .dump();
        return response;
    }
    response.body = nlohmann::json{{"jsonrpc", "2.0"}, {"id", body.contains("id") ? body["id"] : nullptr}, {"result", result}}.dump();
    return response;
}

}  // namespace

GatewayRouter::GatewayRouter(std::shared_ptr<Configuration> config,
                             std::shared_ptr<PaymentProcessor> payment,
                             std::shared_ptr<IdentityVerifier> identity,
                             std::shared_ptr<ReputationChecker> reputation,
                             std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                             std::shared_ptr<BackendClient> backend,
                             std::shared_ptr<gateway::monitoring::MetricsCollector> metrics)
    : config_(std::move(config))
    , payment_(std::move(payment))
    , identity_(std::move(identity))
    , reputation_(std::move(reputation))
    , blockchain_(std::move(blockchain))
    , backend_(std::move(backend))
    , metrics_(std::move(metrics))
    , rate_limiter_(60)
    , clock_([] { return std::chrono::steady_clock::now(); }) {
    if (!config_ || !payment_ || !identity_ || !reputation_ || !blockchain_ || !backend_) {
        throw std::invalid_argument("GatewayRouter requires configuration, verifiers, blockchain, and a backend client");
    }
}

GatewayResponse GatewayRouter::json(int status, std::string body) const {
    GatewayResponse response;
    response.status = status;
    response.body = std::move(body);
    response.content_type = "application/json";
    return response;
}

std::shared_ptr<Configuration> GatewayRouter::configuration() const {
    std::lock_guard lock(config_mutex_);
    return config_;
}

std::chrono::steady_clock::time_point GatewayRouter::now() const {
    std::lock_guard lock(config_mutex_);
    return clock_();
}

void GatewayRouter::applyConfiguration(std::shared_ptr<Configuration> config) {
    if (!config) {
        throw std::invalid_argument("configuration reload is empty");
    }
    {
        std::lock_guard lock(config_mutex_);
        config_ = config;
    }
    payment_->updateConfiguration(config);
    reputation_->updateConfiguration(config);
    if (auto* connector = dynamic_cast<gateway::blockchain::BlockchainConnector*>(blockchain_.get())) {
        connector->reloadNetworkConfigs(config->networks);
        connector->setTreasuryKey(config->treasury_private_key);
    }
    gateway::monitoring::Logger::info("configuration reload applied to in-flight requests");
}

void GatewayRouter::addFilter(std::shared_ptr<GatewayFilter> filter) {
    std::lock_guard lock(config_mutex_);
    filters_.add(std::move(filter));
}

void GatewayRouter::setSpanExporter(std::shared_ptr<gateway::monitoring::SpanExporter> exporter) {
    std::lock_guard lock(config_mutex_);
    span_exporter_ = std::move(exporter);
}

void GatewayRouter::setTimeSource(std::function<std::chrono::steady_clock::time_point()> clock) {
    std::lock_guard lock(config_mutex_);
    clock_ = std::move(clock);
}

std::uint64_t GatewayRouter::inFlight() const {
    return in_flight_.load();
}

bool GatewayRouter::isAccepting() const {
    return accepting_.load();
}

void GatewayRouter::shutdown(std::chrono::seconds drain_timeout) {
    accepting_.store(false);
    gateway::monitoring::Logger::info("initiating graceful shutdown");
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t announced = std::numeric_limits<std::uint64_t>::max();
    while (true) {
        const std::uint64_t remaining = in_flight_.load();
        if (remaining != announced) {
            std::ostringstream line;
            line << "in-flight requests remaining: " << remaining;
            gateway::monitoring::Logger::info(line.str());
            announced = remaining;
        }
        if (remaining == 0) {
            break;
        }
        if (std::chrono::steady_clock::now() - started >= drain_timeout) {
            std::ostringstream line;
            line << "shutdown timeout with in-flight requests remaining: " << remaining;
            gateway::monitoring::Logger::warn(line.str());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    payment_->persistState();
    blockchain_->closeConnections();
    gateway::monitoring::Logger::info("graceful shutdown complete");
}

GatewayResponse GatewayRouter::handle(const ForwardRequest& request) {
    if (!accepting_.load()) {
        return json(503, R"({"error":"gateway is shutting down"})");
    }
    InFlightGuard guard(in_flight_);
    const auto started = now();
    const std::string request_id = "req-" + std::to_string(request_ids_.fetch_add(1) + 1);
    const std::string path = pathOnly(request.target);
    auto finish = [&](GatewayResponse response, const std::string& wallet, double amount, double reputation) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now() - started);
        if (metrics_) {
            metrics_->recordRequest(path, std::to_string(response.status), elapsed);
            metrics_->setActiveConnections(static_cast<double>(in_flight_.load()));
        }
        gateway::monitoring::Logger::logRequest(request_id, wallet, path, amount, reputation, response.status, elapsed);
        std::shared_ptr<gateway::monitoring::SpanExporter> exporter;
        {
            std::lock_guard lock(config_mutex_);
            exporter = span_exporter_;
        }
        if (exporter) {
            gateway::monitoring::SpanRecord record;
            record.method = request.method;
            record.path = path;
            record.status = response.status;
            record.duration = elapsed;
            try {
                exporter->exportSpan(record);
            } catch (const std::exception&) {
            }
        }
        return response;
    };

    const auto config = configuration();
    if (request.method == "GET" && path == "/health") {
        std::vector<std::pair<std::string, bool>> networks;
        for (const auto& network : config->networks) {
            if (network.enabled) {
                networks.emplace_back(NetworkToString(network.network), blockchain_->isNetworkHealthy(network.network));
            }
        }
        const auto report = gateway::monitoring::makeHealthReport(networks);
        auto response = json(report.status_code, report.body);
        return finish(std::move(response), "", 0.0, 0.0);
    }
    if (path == "/mcp" && (request.method == "GET" || request.method == "POST")) {
        return finish(mcpResponse(*config, blockchain_->paymentWallet(), request), "", 0.0, 0.0);
    }
    if (path == "/v1/auth") {
        const auto rewritten = requestFromEdge(request);
        if (!rewritten) {
            return finish(json(400, R"({"error":"X-Original-URI and X-Original-Method are required"})"), "", 0.0, 0.0);
        }
        return handle(*rewritten);
    }

    if (!hasAgentSignature(request.headers)) {
        gateway::monitoring::Logger::logSecurityEvent("missing_signature", "", "agent signature header is absent");
        return finish(json(401, R"({"error":"missing or invalid request signature"})"), "", 0.0, 0.0);
    }

    WalletSource wallet_source;
    for (const HttpHeader& header : request.headers) {
        wallet_source.headers.emplace_back(header.name, header.value);
    }
    const auto wallet = IdentityVerifier::extractWalletAddress(wallet_source);
    if (!wallet) {
        gateway::monitoring::Logger::logSecurityEvent("missing_wallet", "", "wallet address header is absent");
        return finish(json(401, R"({"error":"missing wallet address"})"), "", 0.0, 0.0);
    }

    gateway::blockchain::AgentRequest signed_request;
    signed_request.method = request.method;
    signed_request.path = path;
    signed_request.body = request.body;
    signed_request.wallet = *wallet;
    signed_request.verifying_contract = blockchain_->paymentWallet();
    signed_request.quote_reference = headerValue(request.headers, "X-Payment-Reference").value_or("");
    auto signature_network = blockchain_->selectOptimalNetwork().value_or(Network::BASE);
    if (!signed_request.quote_reference.empty()) {
        const auto quote = payment_->findQuote(signed_request.quote_reference);
        if (!quote) {
            gateway::monitoring::Logger::logSecurityEvent("invalid_signature", *wallet, "quote reference was not issued");
            return finish(json(401, R"({"error":"missing or invalid request signature"})"), *wallet, 0.0, 0.0);
        }
        signed_request.expiry_unix = quote->expires_at_unix;
        try {
            signature_network = NetworkFromString(quote->network);
        } catch (const std::exception&) {
            return finish(json(401, R"({"error":"missing or invalid request signature"})"), *wallet, 0.0, 0.0);
        }
    }
    signed_request.chain_id = gateway::blockchain::chainIdOf(signature_network);
    const auto signature = headerValue(request.headers, "X-Agent-Signature").value_or("");
    const auto digest = gateway::blockchain::agentRequestDigest(signed_request);
    const auto signer = gateway::blockchain::recoverSigner(digest, signature);
    const bool eoa_match = signer && gateway::blockchain::normalizeAddress(*signer) ==
                                         gateway::blockchain::normalizeAddress(*wallet);
    const bool contract_match =
        !eoa_match && blockchain_->isValidContractSignature(*wallet, "0x" + gateway::blockchain::toHex(digest), signature,
                                                            signature_network);
    if (!eoa_match && !contract_match) {
        gateway::monitoring::Logger::logSecurityEvent("invalid_signature", *wallet, "request signature does not recover the wallet");
        return finish(json(401, R"({"error":"missing or invalid request signature"})"), *wallet, 0.0, 0.0);
    }
    if (walletListed(config->wallet_policy.denylist, *wallet)) {
        gateway::monitoring::Logger::logSecurityEvent("denied_wallet", *wallet, "wallet is on the denylist");
        return finish(json(403, R"({"error":"wallet is denied"})"), *wallet, 0.0, 0.0);
    }

    if (request.method == "POST" && path == "/v1/refunds") {
        const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
        if (!body.is_object() || !body.contains("tx_hash") || !body.contains("wallet_address")) {
            return finish(json(400, R"({"error":"tx_hash and wallet_address are required"})"), *wallet, 0.0, 0.0);
        }
        const double gas_fee = body.value("gas_fee_usdc", 0.0);
        const std::string tx_hash = body.value("tx_hash", "");
        const std::string refund_wallet = body.value("wallet_address", "");
        const bool refunded = payment_->processRefund(tx_hash, refund_wallet, gas_fee);
        if (!refunded) {
            return finish(json(409, R"({"error":"refund blocked"})"), *wallet, 0.0, 0.0);
        }
        const auto stored = payment_->findRefund(tx_hash);
        nlohmann::json payload = {{"refunded", true},
                                  {"tx_hash", tx_hash},
                                  {"gas_fee_usdc", gas_fee},
                                  {"refund_amount_usdc", stored ? stored->refund_amount_usdc : 0.0}};
        return finish(json(200, payload.dump()), *wallet, stored ? stored->amount_usdc : 0.0, 0.0);
    }

    if (request.method == "POST" && path == "/v1/credits") {
        const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
        if (!body.is_object() || !body.contains("tx_hash")) {
            return finish(json(400, R"({"error":"tx_hash is required"})"), *wallet, 0.0, 0.0);
        }
        const auto credited = payment_->creditDeposit(*wallet, body.value("tx_hash", ""));
        if (credited.status != PaymentResponse::Status::VERIFIED) {
            const int status = credited.status == PaymentResponse::Status::REPLAY_DETECTED ? 409 : 402;
            return finish(json(status, nlohmann::json{{"error", credited.message}}.dump()), *wallet, 0.0, 0.0);
        }
        return finish(json(200, nlohmann::json{{"credited", true}, {"balance_usdc", payment_->prepaidBalance(*wallet)}}.dump()),
                      *wallet, credited.price_usdc.value_or(0.0), 0.0);
    }

    const EndpointConfig& endpoint = matchEndpoint(*config, path);
    const auto current = now();
    if (!rate_limiter_.checkLimit(*wallet, endpoint.rate_limit_per_minute, current)) {
        const auto retry = rate_limiter_.retryAfter(*wallet, current);
        gateway::monitoring::Logger::logSecurityEvent("rate_limited", *wallet, "sliding window exceeded");
        auto response = json(429, R"({"error":"too many requests"})");
        response.headers.push_back(HttpHeader{"Retry-After", std::to_string(retry.count())});
        return finish(std::move(response), *wallet, endpoint.price_usdc, 0.0);
    }
    rate_limiter_.recordRequest(*wallet, current);

    PaymentRequest payment_request;
    payment_request.endpoint_path = path;
    payment_request.wallet_address = *wallet;
    payment_request.timestamp_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch()).count());
    if (const auto tx_hash = headerValue(request.headers, "X-Payment-Tx")) {
        if (!tx_hash->empty()) {
            payment_request.tx_hash = *tx_hash;
        }
    }
    if (const auto reference = headerValue(request.headers, "X-Payment-Reference")) {
        payment_request.tx_reference = *reference;
    }

    if (!endpoint.allowlist.empty() && !walletListed(endpoint.allowlist, *wallet)) {
        gateway::monitoring::Logger::logSecurityEvent("allowlist", *wallet, "wallet is not on the endpoint allowlist");
        return finish(json(403, R"({"error":"wallet is not on the endpoint allowlist"})"), *wallet, endpoint.price_usdc, 0.0);
    }
    {
        FilterContext context;
        context.phase = FilterPhase::BeforePayment;
        context.request = &request;
        context.wallet = *wallet;
        context.path = path;
        context.endpoint = &endpoint;
        FilterChain chain;
        {
            std::lock_guard lock(config_mutex_);
            chain = filters_;
        }
        const FilterResult filtered = chain.run(context);
        if (!filtered.continue_request) {
            return finish(json(filtered.status, filtered.body), *wallet, endpoint.price_usdc, 0.0);
        }
    }

    const bool prepaid_mode = endpoint.price_usdc > 0.0 && endpoint.gas_budget_usdc > endpoint.price_usdc;
    bool debited = false;
    if (prepaid_mode) {
        if (!payment_->tryDebitPrepaid(*wallet, endpoint.price_usdc)) {
            nlohmann::json payload = {{"error", "payment required"},
                                      {"price_usdc", endpoint.price_usdc},
                                      {"balance_usdc", payment_->prepaidBalance(*wallet)},
                                      {"top_up", "POST /v1/credits"}};
            return finish(json(402, payload.dump()), *wallet, endpoint.price_usdc, 0.0);
        }
        debited = true;
    }
    const auto authorization_header = headerValue(request.headers, "X-Payment-Authorization");
    if (!debited && endpoint.price_usdc > 0.0 && !payment_request.tx_hash && !authorization_header) {
        const PaymentResponse quote = payment_->requestPayment(payment_request);
        nlohmann::json payload = {{"error", "payment required"},
                                  {"price_usdc", quote.price_usdc.value_or(endpoint.price_usdc)},
                                  {"payment_wallet", quote.payment_wallet.value_or("")},
                                  {"network", quote.preferred_network.value_or("")},
                                  {"transaction_reference", quote.transaction_reference.value_or("")},
                                  {"expires_at_unix", quote.expires_at_unix.value_or(0)},
                                  {"payment_header", "X-Payment-Tx"},
                                  {"quote_header", "X-Payment-Reference"}};
        if (metrics_) {
            metrics_->recordPayment(blockchain_->selectOptimalNetwork().value_or(Network::BASE), endpoint.price_usdc,
                                    false);
        }
        return finish(json(402, payload.dump()), *wallet, endpoint.price_usdc, 0.0);
    }

    const auto verification_started = now();
    PaymentResponse verified;
    if (debited || endpoint.price_usdc == 0.0) {
        verified.status = PaymentResponse::Status::VERIFIED;
        verified.message = debited ? "prepaid balance debited" : "zero-price endpoint";
    } else if (authorization_header) {
        const nlohmann::json authorization_json = nlohmann::json::parse(*authorization_header, nullptr, false);
        gateway::blockchain::TransferAuthorization authorization;
        if (!authorization_json.is_object()) {
            verified.status = PaymentResponse::Status::INVALID_TRANSACTION;
            verified.message = "authorization payload is invalid";
        } else {
            authorization.from = *wallet;
            authorization.to = authorization_json.value("to", "");
            authorization.value_units = authorization_json.value("value", "");
            authorization.valid_after = authorization_json.value("valid_after", static_cast<std::uint64_t>(0));
            authorization.valid_before = authorization_json.value("valid_before", static_cast<std::uint64_t>(0));
            authorization.nonce_hex = authorization_json.value("nonce", "");
            authorization.signature_hex = authorization_json.value("signature", "");
            verified = payment_->settleAuthorization(payment_request, authorization, endpoint.price_usdc, current);
        }
    } else {
        verified = payment_->verifyPayment(payment_request, endpoint.price_usdc, current);
    }
    if (verified.settled_tx_hash && !verified.settled_tx_hash->empty()) {
        payment_request.tx_hash = verified.settled_tx_hash;
    }
    if (verified.status != PaymentResponse::Status::VERIFIED) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        }
        const int status = verified.status == PaymentResponse::Status::REPLAY_DETECTED ? 409 : 402;
        if (status == 409) {
            gateway::monitoring::Logger::logSecurityEvent("replay", *wallet, verified.message);
        }
        nlohmann::json payload = {{"error", statusText(verified.status)},
                                  {"detail", verified.message},
                                  {"price_usdc", verified.price_usdc.value_or(endpoint.price_usdc)},
                                  {"payment_wallet", verified.payment_wallet.value_or("")}};
        return finish(json(status, payload.dump()), *wallet, endpoint.price_usdc, 0.0);
    }

    const auto network = blockchain_->selectOptimalNetwork().value_or(Network::BASE);
    const auto identity = identity_->verifyIdentity(*wallet, network);
    if (identity.status != IdentityVerificationResult::Status::VERIFIED || !identity.identity_data) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        }
        return finish(json(403, nlohmann::json{{"error", "identity verification failed"}, {"detail", identity.message}}.dump()),
                      *wallet, endpoint.price_usdc, 0.0);
    }
    const auto unix_now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (!nftMeetsMinimumAge(identity.identity_data->registration_timestamp, config->wallet_policy.minimum_nft_age_seconds,
                            unix_now)) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        }
        return finish(json(403, R"({"error":"identity NFT is newer than the minimum age"})"), *wallet, endpoint.price_usdc, 0.0);
    }

    const auto proof = headerValue(request.headers, "X-Agent-Proof").value_or("");
    const auto reputation = reputation_->checkReputation(*wallet, network, path, proof);
    if (reputation.status != ReputationCheckResult::Status::APPROVED) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        }
        return finish(json(403, nlohmann::json{{"error", "reputation check failed"}, {"detail", reputation.message}}.dump()),
                      *wallet, endpoint.price_usdc, reputation.composite_trust_score);
    }
    if (metrics_) {
        const auto verification_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now() - verification_started);
        metrics_->recordVerificationLatency("pipeline", verification_elapsed);
        metrics_->recordPayment(network, endpoint.price_usdc, true);
    }
    {
        FilterContext context;
        context.phase = FilterPhase::AfterIdentity;
        context.request = &request;
        context.wallet = *wallet;
        context.path = path;
        context.endpoint = &endpoint;
        FilterChain chain;
        {
            std::lock_guard lock(config_mutex_);
            chain = filters_;
        }
        const FilterResult filtered = chain.run(context);
        if (!filtered.continue_request) {
            if (debited) {
                payment_->creditPrepaid(*wallet, endpoint.price_usdc);
            }
            return finish(json(filtered.status, filtered.body), *wallet, endpoint.price_usdc, reputation.composite_trust_score);
        }
    }

    ForwardRequest outbound;
    outbound.method = request.method.empty() ? "GET" : request.method;
    outbound.target = request.target;
    outbound.body = request.body;
    outbound.headers = headersForBackend(request.headers);
    outbound.headers.erase(std::remove_if(outbound.headers.begin(), outbound.headers.end(),
                                          [](const HttpHeader& header) { return ownedByGateway(header.name); }),
                           outbound.headers.end());
    outbound.headers.push_back(HttpHeader{"X-Agent-Wallet", identity.identity_data->owner_address});
    outbound.headers.push_back(HttpHeader{"X-Agent-Identity-Token", identity.identity_data->token_id});
    outbound.headers.push_back(HttpHeader{"X-Agent-Identity-Type", identity.identity_data->identity_type});
    outbound.headers.push_back(HttpHeader{"X-Agent-Reputation", std::to_string(reputation.composite_trust_score)});
    outbound.headers.push_back(HttpHeader{"X-Agent-Network", NetworkToString(network)});
    outbound.headers.push_back(
        HttpHeader{"X-Agent-Requests", std::to_string(reputation.reputation_data ? reputation.reputation_data->total_requests : 0)});
    {
        FilterContext context;
        context.phase = FilterPhase::BeforeForward;
        context.request = &request;
        context.wallet = *wallet;
        context.path = path;
        context.endpoint = &endpoint;
        FilterChain chain;
        {
            std::lock_guard lock(config_mutex_);
            chain = filters_;
        }
        const FilterResult filtered = chain.run(context);
        if (!filtered.continue_request) {
            if (debited) {
                payment_->creditPrepaid(*wallet, endpoint.price_usdc);
            }
            return finish(json(filtered.status, filtered.body), *wallet, endpoint.price_usdc, reputation.composite_trust_score);
        }
        for (const HttpHeader& header : context.added_headers) {
            outbound.headers.push_back(header);
        }
    }
    if (request.decision_only) {
        GatewayResponse response = json(200, R"({"allowed":true})");
        for (const HttpHeader& header : outbound.headers) {
            if (ownedByGateway(header.name)) {
                response.headers.push_back(header);
            }
        }
        response.headers.push_back(HttpHeader{"X-Payment-Amount", std::to_string(endpoint.price_usdc)});
        response.headers.push_back(HttpHeader{"X-Transaction-Hash", payment_request.tx_hash.value_or("")});
        return finish(std::move(response), *wallet, endpoint.price_usdc, reputation.composite_trust_score);
    }

    const auto connect_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::min(endpoint.backend_timeout, std::chrono::seconds{5}));
    const ForwardResult backend = backend_->forward(endpoint.backend_url, outbound, connect_timeout, std::chrono::seconds{30});
    const std::string tx_hash = payment_request.tx_hash.value_or("");
    if (backend.kind == ForwardResult::Kind::ConnectionFailed) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        } else if (!tx_hash.empty()) {
            payment_->markRefundable(tx_hash, *wallet, endpoint.price_usdc, "backend_connection_failed");
        }
        if (metrics_) {
            metrics_->recordError("backend_connection");
        }
        return finish(json(502, R"({"error":"backend connection failed"})"), *wallet, endpoint.price_usdc,
                      reputation.composite_trust_score);
    }
    if (backend.kind == ForwardResult::Kind::Timeout) {
        if (debited) {
            payment_->creditPrepaid(*wallet, endpoint.price_usdc);
        } else if (!tx_hash.empty()) {
            payment_->markRefundable(tx_hash, *wallet, endpoint.price_usdc, "backend_timeout");
        }
        if (metrics_) {
            metrics_->recordError("backend_timeout");
        }
        return finish(json(504, R"({"error":"backend timeout"})"), *wallet, endpoint.price_usdc,
                      reputation.composite_trust_score);
    }

    GatewayResponse response;
    response.status = backend.status;
    response.body = backend.body;
    response.headers = headersForBackend(backend.headers);
    response.content_type = "application/octet-stream";
    for (const HttpHeader& header : backend.headers) {
        if (header.name.size() == 12) {
            const auto content_type = headerValue(backend.headers, "Content-Type");
            if (content_type) {
                response.content_type = *content_type;
            }
            break;
        }
    }
    if (const auto content_type = headerValue(backend.headers, "Content-Type")) {
        response.content_type = *content_type;
    }
    response.headers.push_back(HttpHeader{"X-Payment-Amount", std::to_string(endpoint.price_usdc)});
    response.headers.push_back(HttpHeader{"X-Transaction-Hash", tx_hash});
    if (backend.status >= 500 && debited) {
        payment_->creditPrepaid(*wallet, endpoint.price_usdc);
    } else if (backend.status >= 500 && !tx_hash.empty()) {
        payment_->markRefundable(tx_hash, *wallet, endpoint.price_usdc, "backend_5xx");
        if (metrics_) {
            metrics_->recordError("backend_5xx");
        }
    }
    return finish(std::move(response), *wallet, endpoint.price_usdc, reputation.composite_trust_score);
}

}  // namespace gateway::core
