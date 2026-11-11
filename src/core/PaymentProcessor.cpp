#include "gateway/core/PaymentProcessor.h"

#include "gateway/blockchain/hex.h"
#include "gateway/core/validation_rules.h"
#include "gateway/utils/bloom_filter.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace gateway::core {
namespace {

constexpr double kUsdcScale = 1000000.0;

std::int64_t toBaseUnits(double usdc) {
    return std::llround(usdc * kUsdcScale);
}

bool unitsCoverQuote(std::int64_t paid, std::int64_t quoted) {
    if (paid < 0 || quoted < 0) {
        return false;
    }
    return paid >= (quoted * 999) / 1000;
}

std::optional<std::int64_t> parseBaseUnits(const std::string& decimal) {
    if (decimal.empty() || decimal.size() > 18) {
        return std::nullopt;
    }
    for (const char c : decimal) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
    }
    try {
        return std::stoll(decimal);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

bool amountCoversQuote(double paid_usdc, double quoted_usdc) {
    if (!std::isfinite(paid_usdc) || !std::isfinite(quoted_usdc) || paid_usdc < 0.0 || quoted_usdc < 0.0) {
        return false;
    }
    return unitsCoverQuote(toBaseUnits(paid_usdc), toBaseUnits(quoted_usdc));
}

const EndpointConfig& matchEndpoint(const Configuration& config, const std::string& path) {
    for (const EndpointConfig& endpoint : config.endpoints) {
        try {
            if (std::regex_match(path, std::regex(endpoint.path_pattern))) {
                return endpoint;
            }
        } catch (const std::regex_error&) {
        }
    }
    return config.default_endpoint;
}

PaymentProcessor::PaymentProcessor(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                                   std::shared_ptr<Configuration> config,
                                   std::filesystem::path refund_store_path)
    : blockchain_(std::move(blockchain))
    , config_(std::move(config))
    , refund_store_path_(std::move(refund_store_path))
    , verified_cache_(4096, std::chrono::duration_cast<std::chrono::seconds>(kVerifiedPaymentTtl)) {
    if (!blockchain_ || !config_) {
        throw std::invalid_argument("PaymentProcessor requires blockchain and configuration");
    }
    const auto bits = static_cast<std::size_t>(config_->security.bloom_filter_size_mb) * 1024U * 1024U * 8U;
    replay_filter_ = std::make_unique<gateway::utils::BloomFilter>(bits == 0 ? 8192 : bits, 5);
    loadState();
}

PaymentResponse PaymentProcessor::requestPayment(const PaymentRequest& request) {
    const std::shared_ptr<Configuration> config = configuration();
    const EndpointConfig& endpoint = matchEndpoint(*config, request.endpoint_path);
    PaymentResponse response;
    response.status = PaymentResponse::Status::NOT_PAID;
    response.price_usdc = endpoint.price_usdc;
    response.payment_wallet = blockchain_->paymentWallet();
    response.transaction_reference = nextReference();
    if (const auto network = blockchain_->selectOptimalNetwork()) {
        response.preferred_network = NetworkToString(*network);
        response.message = "payment required";
    } else {
        response.message = "no healthy payment network";
    }
    return response;
}

PaymentResponse PaymentProcessor::verifyPayment(const PaymentRequest& request,
                                                double endpoint_price,
                                                std::chrono::steady_clock::time_point now) {
    PaymentResponse response;
    response.payment_wallet = blockchain_->paymentWallet();
    response.price_usdc = endpoint_price;
    if (endpoint_price == 0.0) {
        response.status = PaymentResponse::Status::VERIFIED;
        response.message = "zero-price endpoint";
        return response;
    }
    if (!request.tx_hash || request.tx_hash->empty()) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "missing transaction metadata";
        return response;
    }
    const std::string& hash = *request.tx_hash;
    {
        std::lock_guard lock(mutex_);
        if (const auto cached = verified_until_.find(hash);
            cached != verified_until_.end() && now < cached->second.expires_at) {
            response.status = PaymentResponse::Status::VERIFIED;
            response.message = "cached payment verification";
            response.transaction_reference = hash;
            return response;
        }
        if (seen_transactions_.contains(hash)) {
            response.status = PaymentResponse::Status::REPLAY_DETECTED;
            response.message = "transaction hash already used";
            PendingVerification pending;
            pending.tx_hash = hash;
            pending.wallet_address = request.wallet_address;
            pending.endpoint_path = request.endpoint_path;
            pending.price_usdc = endpoint_price;
            pending.status = "replay";
            pending.updated_at_unix = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
            upsertPendingUnlocked(std::move(pending));
            saveStateUnlocked();
            return response;
        }
        PendingVerification pending;
        pending.tx_hash = hash;
        pending.wallet_address = request.wallet_address;
        pending.endpoint_path = request.endpoint_path;
        pending.price_usdc = endpoint_price;
        pending.status = "pending";
        pending.updated_at_unix = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        upsertPendingUnlocked(pending);
        saveStateUnlocked();
    }

    const auto network = blockchain_->selectOptimalNetwork().value_or(Network::BASE);
    const auto lookup = blockchain_->getTransaction(hash, network);
    if (lookup.status == gateway::blockchain::TxStatus::MissingMetadata) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "missing transaction metadata";
        return response;
    }
    if (lookup.status == gateway::blockchain::TxStatus::NotFound || !lookup.transaction) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = lookup.status == gateway::blockchain::TxStatus::NetworkUnavailable
                               ? "payment network unavailable"
                               : "transaction not found";
        return response;
    }
    const auto& tx = *lookup.transaction;
    if (!tx.execution_succeeded) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "transaction reverted";
        return response;
    }
    if (!tx.is_finalized) {
        response.status = PaymentResponse::Status::VERIFICATION_PENDING;
        response.message = "transaction is not finalized";
        return response;
    }
    if (!isValidAddress(tx.to_address) ||
        gateway::blockchain::normalizeAddress(tx.to_address) !=
            gateway::blockchain::normalizeAddress(*response.payment_wallet)) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment destination does not match the gateway wallet";
        return response;
    }
    const auto paid = parseBaseUnits(tx.value_wei);
    if (!paid || !unitsCoverQuote(*paid, toBaseUnits(endpoint_price))) {
        response.status = PaymentResponse::Status::INSUFFICIENT_AMOUNT;
        response.message = "payment amount is below the quoted price";
        return response;
    }

    {
        std::lock_guard lock(mutex_);
        if (seen_transactions_.contains(hash)) {
            response.status = PaymentResponse::Status::REPLAY_DETECTED;
            response.message = "transaction hash already used";
            return response;
        }
        seen_transactions_.insert(hash);
        replay_filter_->add(hash);
        const auto expires_unix = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                (std::chrono::system_clock::now() + kVerifiedPaymentTtl).time_since_epoch())
                .count());
        verified_until_[hash] = CachedProof{now + kVerifiedPaymentTtl, expires_unix};
        verified_cache_.put(hash, "verified", std::chrono::duration_cast<std::chrono::seconds>(kVerifiedPaymentTtl));
        PendingVerification pending;
        pending.tx_hash = hash;
        pending.wallet_address = request.wallet_address;
        pending.endpoint_path = request.endpoint_path;
        pending.price_usdc = endpoint_price;
        pending.status = "verified";
        pending.updated_at_unix = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        upsertPendingUnlocked(std::move(pending));
        saveStateUnlocked();
    }
    response.status = PaymentResponse::Status::VERIFIED;
    response.message = "payment verified";
    response.transaction_reference = hash;
    response.preferred_network = NetworkToString(network);
    return response;
}

void PaymentProcessor::markRefundable(const std::string& tx_hash,
                                      const std::string& wallet_address,
                                      double amount_usdc,
                                      const std::string& reason) {
    std::lock_guard lock(mutex_);
    for (auto& refund : refunds_) {
        if (refund.tx_hash == tx_hash) {
            refund.reason = reason;
            saveStateUnlocked();
            return;
        }
    }
    gateway::models::RefundableTransaction record;
    record.tx_hash = tx_hash;
    record.wallet_address = wallet_address;
    record.amount_usdc = amount_usdc;
    record.reason = reason;
    record.network = blockchain_->selectOptimalNetwork().value_or(Network::BASE);
    record.refund_requested_at = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    refunds_.push_back(std::move(record));
    saveStateUnlocked();
}

bool PaymentProcessor::processRefund(const std::string& tx_hash,
                                     const std::string& wallet_address,
                                     double gas_fee_usdc) {
    std::lock_guard lock(mutex_);
    if (gas_fee_usdc < 0.0) {
        return false;
    }
    for (auto& refund : refunds_) {
        if (refund.tx_hash != tx_hash || refund.refund_processed) {
            continue;
        }
        if (gateway::blockchain::normalizeAddress(refund.wallet_address) !=
            gateway::blockchain::normalizeAddress(wallet_address)) {
            return false;
        }
        if (!(gas_fee_usdc < refund.amount_usdc)) {
            return false;
        }
        refund.gas_fee_usdc = gas_fee_usdc;
        refund.refund_amount_usdc = refund.amount_usdc - gas_fee_usdc;
        refund.refund_processed = true;
        saveStateUnlocked();
        return true;
    }
    return false;
}

std::optional<gateway::models::RefundableTransaction> PaymentProcessor::findRefund(const std::string& tx_hash) const {
    std::lock_guard lock(mutex_);
    for (const auto& refund : refunds_) {
        if (refund.tx_hash == tx_hash) {
            return refund;
        }
    }
    return std::nullopt;
}

void PaymentProcessor::updateConfiguration(std::shared_ptr<Configuration> config) {
    if (!config) {
        throw std::invalid_argument("PaymentProcessor configuration is empty");
    }
    std::lock_guard lock(mutex_);
    config_ = std::move(config);
}

void PaymentProcessor::persistState() const {
    std::lock_guard lock(mutex_);
    saveStateUnlocked();
}

std::vector<PendingVerification> PaymentProcessor::pendingVerifications() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

std::shared_ptr<Configuration> PaymentProcessor::configuration() const {
    std::lock_guard lock(mutex_);
    return config_;
}

void PaymentProcessor::upsertPendingUnlocked(PendingVerification pending) {
    for (auto& existing : pending_) {
        if (existing.tx_hash == pending.tx_hash) {
            existing = std::move(pending);
            return;
        }
    }
    pending_.push_back(std::move(pending));
}

void PaymentProcessor::loadState() {
    if (refund_store_path_.empty() || !std::filesystem::exists(refund_store_path_)) {
        return;
    }
    std::ifstream input(refund_store_path_);
    if (!input) {
        return;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const PersistedGatewayState loaded = parseGatewayState(buffer.str());
    refunds_ = loaded.refunds;
    pending_ = loaded.pending;
    const auto now_unix = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    for (const std::string& hash : loaded.seen_transactions) {
        seen_transactions_.insert(hash);
        replay_filter_->add(hash);
    }
    for (const auto& item : loaded.verified_until_unix) {
        if (item.second > now_unix) {
            const auto remaining = std::chrono::seconds{item.second - now_unix};
            verified_until_[item.first] = CachedProof{std::chrono::steady_clock::now() + remaining, item.second};
            verified_cache_.put(item.first, "verified", std::chrono::duration_cast<std::chrono::seconds>(remaining));
        }
    }
}

void PaymentProcessor::saveStateUnlocked() const {
    if (refund_store_path_.empty()) {
        return;
    }
    PersistedGatewayState state;
    state.refunds = refunds_;
    state.pending = pending_;
    state.seen_transactions.assign(seen_transactions_.begin(), seen_transactions_.end());
    state.verified_until_unix.reserve(verified_until_.size());
    for (const auto& item : verified_until_) {
        state.verified_until_unix.emplace_back(item.first, item.second.expires_at_unix);
    }
    if (refund_store_path_.has_parent_path()) {
        std::filesystem::create_directories(refund_store_path_.parent_path());
    }
    std::ofstream output(refund_store_path_, std::ios::trunc);
    output << serializeGatewayState(state);
}

std::string PaymentProcessor::nextReference() {
    const auto id = reference_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    std::ostringstream out;
    out << "txref-" << std::hex << id << "-"
        << std::chrono::steady_clock::now().time_since_epoch().count();
    return out.str();
}

}  // namespace gateway::core
