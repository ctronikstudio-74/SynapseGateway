#include "gateway/core/PaymentProcessor.h"

#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/request_signature.h"
#include "gateway/blockchain/settlement.h"
#include "gateway/core/validation_rules.h"
#include "gateway/utils/bloom_filter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gateway::core {
namespace {

struct AcceptedStablecoin {
    std::string contract;
    std::uint32_t decimals = 6;
};

std::vector<AcceptedStablecoin> acceptedStablecoins(const BlockchainNetworkConfig& network) {
    std::vector<AcceptedStablecoin> tokens;
    if (!network.usdc_contract.empty()) {
        tokens.push_back(AcceptedStablecoin{network.usdc_contract, network.usdc_decimals == 0 ? 6 : network.usdc_decimals});
    }
    for (const StablecoinConfig& coin : network.stablecoins) {
        tokens.push_back(AcceptedStablecoin{coin.contract, coin.decimals == 0 ? 6 : coin.decimals});
    }
    return tokens;
}

std::optional<AcceptedStablecoin> matchStablecoin(const std::vector<AcceptedStablecoin>& tokens,
                                                 std::string_view contract) {
    for (const AcceptedStablecoin& token : tokens) {
        if (gateway::blockchain::normalizeAddress(token.contract) == gateway::blockchain::normalizeAddress(contract)) {
            return token;
        }
    }
    return std::nullopt;
}

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

std::string stripZeros(std::string value) {
    const auto first = value.find_first_not_of('0');
    if (first == std::string::npos) {
        return "0";
    }
    return value.substr(first);
}

std::string quoteBaseUnits(double usdc, std::uint32_t decimals) {
    if (!std::isfinite(usdc) || usdc < 0.0 || decimals < 1 || decimals > 18) {
        return {};
    }
    const auto six = toBaseUnits(usdc);
    if (six < 0) {
        return {};
    }
    std::string units = std::to_string(six);
    if (decimals > 6) {
        units.append(static_cast<std::size_t>(decimals - 6), '0');
    } else if (decimals < 6) {
        const auto drop = static_cast<std::size_t>(6 - decimals);
        if (units.size() <= drop) {
            return "0";
        }
        units.erase(units.size() - drop);
    }
    return stripZeros(units);
}

bool decimalDigits(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int compareDigits(const std::string& left, const std::string& right) {
    const std::string a = stripZeros(left);
    const std::string b = stripZeros(right);
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    if (a == b) {
        return 0;
    }
    return a < b ? -1 : 1;
}

std::string multiplyDigits(const std::string& value, int factor) {
    std::string out(value.size() + 4, '0');
    int carry = 0;
    int index = static_cast<int>(out.size()) - 1;
    for (int cursor = static_cast<int>(value.size()) - 1; cursor >= 0 || carry > 0; --cursor, --index) {
        const int digit = carry + (cursor >= 0 ? (value[static_cast<std::size_t>(cursor)] - '0') * factor : 0);
        out[static_cast<std::size_t>(index)] = static_cast<char>('0' + (digit % 10));
        carry = digit / 10;
    }
    return stripZeros(out);
}

double unitsToDecimal(const std::string& units, std::uint32_t decimals) {
    if (!decimalDigits(units)) {
        return 0.0;
    }
    std::string text = stripZeros(units);
    if (decimals == 0) {
        return std::stod(text);
    }
    if (text.size() <= decimals) {
        text.insert(0, decimals + 1 - text.size(), '0');
    }
    text.insert(text.size() - decimals, 1, '.');
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        return 0.0;
    }
}

bool baseUnitsCoverQuote(const std::string& paid, const std::string& quoted) {
    if (!decimalDigits(paid) || !decimalDigits(quoted)) {
        return false;
    }
    return compareDigits(multiplyDigits(paid, 1000), multiplyDigits(quoted, 999)) >= 0;
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

PaymentResponse PaymentProcessor::requestPayment(const PaymentRequest& request,
                                                  std::chrono::steady_clock::time_point now) {
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
    const auto expires_at = now + kQuoteTtl;
    response.expires_at_unix = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() + kQuoteTtl).time_since_epoch())
            .count());
    IssuedQuote quote;
    quote.reference = *response.transaction_reference;
    quote.endpoint_path = request.endpoint_path;
    quote.wallet_address = request.wallet_address;
    quote.network = response.preferred_network.value_or("");
    quote.price_usdc = endpoint.price_usdc;
    quote.expires_at_unix = *response.expires_at_unix;
    {
        std::lock_guard lock(mutex_);
        for (auto issued = quotes_.begin(); issued != quotes_.end();) {
            if (!(now < issued->second.expires_at)) {
                issued = quotes_.erase(issued);
            } else {
                ++issued;
            }
        }
        quotes_[quote.reference] = OpenQuote{quote, expires_at};
        saveStateUnlocked();
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
            const CachedProof& proof = cached->second;
            const bool same_quote = proof.endpoint_path == request.endpoint_path &&
                                    proof.reference == request.tx_reference &&
                                    std::abs(proof.price_usdc - endpoint_price) <= 1e-9 &&
                                    gateway::blockchain::normalizeAddress(proof.wallet_address) ==
                                        gateway::blockchain::normalizeAddress(request.wallet_address);
            if (!same_quote) {
                response.status = PaymentResponse::Status::INVALID_TRANSACTION;
                response.message = "transaction hash is bound to another quote";
                return response;
            }
            response.status = PaymentResponse::Status::VERIFIED;
            response.message = "cached payment verification";
            response.transaction_reference = proof.reference;
            response.settled_tx_hash = hash;
            response.preferred_network = proof.network;
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

    IssuedQuote quote;
    {
        std::lock_guard lock(mutex_);
        const auto issued = quotes_.find(request.tx_reference);
        if (request.tx_reference.empty() || issued == quotes_.end()) {
            response.status = PaymentResponse::Status::INVALID_TRANSACTION;
            response.message = "payment quote was not issued";
            return response;
        }
        if (!(now < issued->second.expires_at)) {
            response.status = PaymentResponse::Status::INVALID_TRANSACTION;
            response.message = "payment quote has expired";
            return response;
        }
        quote = issued->second.quote;
    }
    if (quote.endpoint_path != request.endpoint_path || std::abs(quote.price_usdc - endpoint_price) > 1e-9 ||
        gateway::blockchain::normalizeAddress(quote.wallet_address) !=
            gateway::blockchain::normalizeAddress(request.wallet_address)) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment quote does not match this request";
        return response;
    }
    if (quote.network.empty()) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment quote has no network";
        return response;
    }
    Network network = Network::BASE;
    try {
        network = NetworkFromString(quote.network);
    } catch (const std::exception&) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment quote has no network";
        return response;
    }
    const auto lookup = blockchain_->getPaymentTransfer(hash, network, request.wallet_address);
    std::vector<AcceptedStablecoin> accepted;
    if (const auto config = configuration()) {
        for (const BlockchainNetworkConfig& item : config->networks) {
            if (item.network == network) {
                accepted = acceptedStablecoins(item);
                break;
            }
        }
    }
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
    const auto matched = matchStablecoin(accepted, tx.token_contract);
    if (!accepted.empty() && !matched) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment is not a transfer of a configured stablecoin";
        return response;
    }
    if (!accepted.empty() &&
        gateway::blockchain::normalizeAddress(tx.from_address) !=
            gateway::blockchain::normalizeAddress(request.wallet_address)) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "stablecoin transfer was not sent by the agent wallet";
        return response;
    }
    const std::uint32_t token_decimals = matched ? matched->decimals : 6;
    const std::string quoted_units = quoteBaseUnits(endpoint_price, token_decimals);
    if (!baseUnitsCoverQuote(tx.value_wei, quoted_units)) {
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
        CachedProof proof;
        proof.expires_at = now + kVerifiedPaymentTtl;
        proof.expires_at_unix = expires_unix;
        proof.endpoint_path = request.endpoint_path;
        proof.wallet_address = request.wallet_address;
        proof.network = quote.network;
        proof.reference = quote.reference;
        proof.price_usdc = endpoint_price;
        verified_until_[hash] = std::move(proof);
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
    response.transaction_reference = quote.reference;
    response.settled_tx_hash = hash;
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
    if (gas_fee_usdc < 0.0) {
        return false;
    }
    gateway::models::RefundableTransaction snapshot;
    bool found = false;
    {
        std::lock_guard lock(mutex_);
        for (const auto& refund : refunds_) {
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
            snapshot = refund;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    if (snapshot.refund_tx_hash == "pending") {
        return false;
    }
    const double refund_amount = snapshot.amount_usdc - gas_fee_usdc;
    std::string usdc;
    std::uint32_t decimals = 6;
    if (const auto config = configuration()) {
        for (const auto& network : config->networks) {
            if (network.network == snapshot.network) {
                usdc = network.usdc_contract;
                decimals = network.usdc_decimals == 0 ? 6 : network.usdc_decimals;
            }
        }
    }
    const auto treasury = configuration() ? gateway::blockchain::addressFromPrivateKey(configuration()->treasury_private_key)
                                          : std::nullopt;
    const bool can_broadcast = treasury && gateway::core::isValidAddress(usdc) && snapshot.refund_tx_hash != "pending";
    std::string refund_hash = snapshot.refund_tx_hash == "pending" ? std::string{} : snapshot.refund_tx_hash;
    if (refund_hash.empty() && can_broadcast) {
        {
            std::lock_guard lock(mutex_);
            for (auto& refund : refunds_) {
                if (refund.tx_hash == tx_hash && !refund.refund_processed && refund.refund_tx_hash.empty()) {
                    refund.refund_tx_hash = "pending";
                    refund.gas_fee_usdc = gas_fee_usdc;
                    refund.refund_amount_usdc = refund_amount;
                }
            }
            saveStateUnlocked();
        }
        const auto broadcast = blockchain_->submitErc20Transfer(snapshot.network, usdc, wallet_address,
                                                                quoteBaseUnits(refund_amount, decimals));
        if (broadcast.status == gateway::blockchain::Broadcast::Status::NotConfigured) {
            std::lock_guard lock(mutex_);
            for (auto& refund : refunds_) {
                if (refund.tx_hash == tx_hash && !refund.refund_processed) {
                    refund.gas_fee_usdc = gas_fee_usdc;
                    refund.refund_amount_usdc = refund_amount;
                    refund.refund_tx_hash.clear();
                    refund.refund_processed = true;
                    saveStateUnlocked();
                    return true;
                }
            }
            return false;
        }
        if (broadcast.status != gateway::blockchain::Broadcast::Status::Submitted || broadcast.hash.empty()) {
            std::lock_guard lock(mutex_);
            for (auto& refund : refunds_) {
                if (refund.tx_hash == tx_hash && refund.refund_tx_hash == "pending") {
                    refund.refund_tx_hash.clear();
                    saveStateUnlocked();
                }
            }
            return false;
        }
        refund_hash = broadcast.hash;
        std::lock_guard lock(mutex_);
        for (auto& refund : refunds_) {
            if (refund.tx_hash == tx_hash) {
                refund.refund_tx_hash = refund_hash;
                refund.gas_fee_usdc = gas_fee_usdc;
                refund.refund_amount_usdc = refund_amount;
            }
        }
        saveStateUnlocked();
    }
    if (!refund_hash.empty() && treasury) {
        const auto lookup = blockchain_->getTokenTransfer(refund_hash, snapshot.network, usdc, *treasury, wallet_address);
        const auto expected = quoteBaseUnits(refund_amount, decimals);
        if (lookup.status != gateway::blockchain::TxStatus::Found || !lookup.transaction ||
            !lookup.transaction->execution_succeeded || !lookup.transaction->is_finalized ||
            compareDigits(lookup.transaction->value_wei, expected) < 0) {
            return false;
        }
        std::lock_guard lock(mutex_);
        for (auto& refund : refunds_) {
            if (refund.tx_hash == tx_hash && !refund.refund_processed) {
                refund.gas_fee_usdc = gas_fee_usdc;
                refund.refund_amount_usdc = refund_amount;
                refund.refund_tx_hash = refund_hash;
                refund.refund_processed = true;
                saveStateUnlocked();
                return true;
            }
        }
        return false;
    }
    std::lock_guard lock(mutex_);
    for (auto& refund : refunds_) {
        if (refund.tx_hash == tx_hash && !refund.refund_processed) {
            refund.gas_fee_usdc = gas_fee_usdc;
            refund.refund_amount_usdc = refund_amount;
            refund.refund_processed = true;
            saveStateUnlocked();
            return true;
        }
    }
    return false;
}

PaymentResponse PaymentProcessor::settleAuthorization(
    const PaymentRequest& request,
    const gateway::blockchain::TransferAuthorization& authorization,
    double endpoint_price,
    std::chrono::steady_clock::time_point now) {
    PaymentResponse response;
    response.payment_wallet = blockchain_->paymentWallet();
    response.price_usdc = endpoint_price;
    IssuedQuote quote;
    std::string existing_hash;
    {
        std::lock_guard lock(mutex_);
        const auto issued = quotes_.find(request.tx_reference);
        if (request.tx_reference.empty() || issued == quotes_.end() || !(now < issued->second.expires_at)) {
            response.status = PaymentResponse::Status::INVALID_TRANSACTION;
            response.message = "payment quote was not issued";
            return response;
        }
        quote = issued->second.quote;
        const auto previous = authorization_txs_.find(authorization.nonce_hex);
        if (previous != authorization_txs_.end()) {
            existing_hash = previous->second;
        }
    }
    if (quote.endpoint_path != request.endpoint_path || std::abs(quote.price_usdc - endpoint_price) > 1e-9 ||
        gateway::blockchain::normalizeAddress(quote.wallet_address) !=
            gateway::blockchain::normalizeAddress(request.wallet_address) ||
        quote.network.empty()) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment quote does not match this request";
        return response;
    }
    if (!existing_hash.empty()) {
        PaymentRequest replay = request;
        replay.tx_hash = existing_hash;
        return verifyPayment(replay, endpoint_price, now);
    }
    Network network = Network::BASE;
    try {
        network = NetworkFromString(quote.network);
    } catch (const std::exception&) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment quote has no network";
        return response;
    }
    std::string usdc;
    std::string domain_name = "USD Coin";
    std::string domain_version = "2";
    std::uint32_t decimals = 6;
    if (const auto config = configuration()) {
        for (const auto& item : config->networks) {
            if (item.network == network) {
                usdc = item.usdc_contract;
                domain_name = item.usdc_eip712_name;
                domain_version = item.usdc_eip712_version;
                decimals = item.usdc_decimals == 0 ? 6 : item.usdc_decimals;
            }
        }
    }
    if (!gateway::core::isValidAddress(usdc) ||
        gateway::blockchain::normalizeAddress(authorization.from) !=
            gateway::blockchain::normalizeAddress(request.wallet_address) ||
        gateway::blockchain::normalizeAddress(authorization.to) !=
            gateway::blockchain::normalizeAddress(*response.payment_wallet)) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "authorization does not pay the gateway wallet";
        return response;
    }
    if (!baseUnitsCoverQuote(authorization.value_units, quoteBaseUnits(endpoint_price, decimals))) {
        response.status = PaymentResponse::Status::INSUFFICIENT_AMOUNT;
        response.message = "payment amount is below the quoted price";
        return response;
    }
    const auto unix_now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (authorization.valid_after > unix_now || authorization.valid_before <= unix_now) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "authorization is outside its validity window";
        return response;
    }
    const auto digest = gateway::blockchain::transferAuthorizationDigest(authorization, domain_name, domain_version,
                                                                        gateway::blockchain::chainIdOf(network), usdc);
    const auto signer = gateway::blockchain::recoverSigner(digest, authorization.signature_hex);
    const bool signed_by_agent =
        signer && gateway::blockchain::normalizeAddress(*signer) ==
                      gateway::blockchain::normalizeAddress(request.wallet_address);
    const bool contract_signed =
        !signed_by_agent &&
        blockchain_->isValidContractSignature(request.wallet_address, "0x" + gateway::blockchain::toHex(digest),
                                              authorization.signature_hex, network);
    if (!signed_by_agent && !contract_signed) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "authorization signature does not recover the agent wallet";
        return response;
    }
    const auto broadcast = blockchain_->submitTransferAuthorization(network, authorization);
    if (broadcast.status != gateway::blockchain::Broadcast::Status::Submitted || broadcast.hash.empty()) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = broadcast.detail.empty() ? "authorization broadcast failed" : broadcast.detail;
        return response;
    }
    {
        std::lock_guard lock(mutex_);
        authorization_txs_[authorization.nonce_hex] = broadcast.hash;
        saveStateUnlocked();
    }
    PaymentRequest paid = request;
    paid.tx_hash = broadcast.hash;
    return verifyPayment(paid, endpoint_price, now);
}

PaymentResponse PaymentProcessor::creditDeposit(const std::string& wallet_address, const std::string& tx_hash) {
    PaymentResponse response;
    response.payment_wallet = blockchain_->paymentWallet();
    if (tx_hash.empty()) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "missing transaction metadata";
        return response;
    }
    {
        std::lock_guard lock(mutex_);
        if (seen_transactions_.contains(tx_hash)) {
            response.status = PaymentResponse::Status::REPLAY_DETECTED;
            response.message = "transaction hash already used";
            return response;
        }
    }
    const auto network = blockchain_->selectOptimalNetwork().value_or(Network::BASE);
    std::vector<AcceptedStablecoin> accepted;
    if (const auto config = configuration()) {
        for (const auto& item : config->networks) {
            if (item.network == network) {
                accepted = acceptedStablecoins(item);
            }
        }
    }
    const auto lookup = blockchain_->getPaymentTransfer(tx_hash, network, wallet_address);
    if (lookup.status != gateway::blockchain::TxStatus::Found || !lookup.transaction ||
        !lookup.transaction->execution_succeeded || !lookup.transaction->is_finalized) {
        response.status = lookup.status == gateway::blockchain::TxStatus::Found
                              ? PaymentResponse::Status::VERIFICATION_PENDING
                              : PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "deposit is not finalized";
        return response;
    }
    const auto matched = matchStablecoin(accepted, lookup.transaction->token_contract);
    if (!accepted.empty() && !matched) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "payment is not a transfer of a configured stablecoin";
        return response;
    }
    if (gateway::blockchain::normalizeAddress(lookup.transaction->from_address) !=
        gateway::blockchain::normalizeAddress(wallet_address)) {
        response.status = PaymentResponse::Status::INVALID_TRANSACTION;
        response.message = "stablecoin transfer was not sent by the agent wallet";
        return response;
    }
    const std::uint32_t decimals = matched ? matched->decimals : 6;
    const double credited = unitsToDecimal(lookup.transaction->value_wei, decimals);
    if (!(credited > 0.0)) {
        response.status = PaymentResponse::Status::INSUFFICIENT_AMOUNT;
        response.message = "payment amount is below the quoted price";
        return response;
    }
    std::lock_guard lock(mutex_);
    if (seen_transactions_.contains(tx_hash)) {
        response.status = PaymentResponse::Status::REPLAY_DETECTED;
        response.message = "transaction hash already used";
        return response;
    }
    seen_transactions_.insert(tx_hash);
    replay_filter_->add(tx_hash);
    const auto key = gateway::blockchain::normalizeAddress(wallet_address);
    credits_[key] += credited;
    saveStateUnlocked();
    response.status = PaymentResponse::Status::VERIFIED;
    response.message = "prepaid balance credited";
    response.price_usdc = credits_[key];
    return response;
}

bool PaymentProcessor::tryDebitPrepaid(const std::string& wallet_address, double amount_usdc) {
    if (!(amount_usdc > 0.0)) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const auto key = gateway::blockchain::normalizeAddress(wallet_address);
    const auto found = credits_.find(key);
    if (found == credits_.end() || found->second + 1e-12 < amount_usdc) {
        return false;
    }
    found->second -= amount_usdc;
    saveStateUnlocked();
    return true;
}

void PaymentProcessor::creditPrepaid(const std::string& wallet_address, double amount_usdc) {
    if (!(amount_usdc > 0.0)) {
        return;
    }
    std::lock_guard lock(mutex_);
    credits_[gateway::blockchain::normalizeAddress(wallet_address)] += amount_usdc;
    saveStateUnlocked();
}

double PaymentProcessor::prepaidBalance(const std::string& wallet_address) const {
    std::lock_guard lock(mutex_);
    const auto found = credits_.find(gateway::blockchain::normalizeAddress(wallet_address));
    return found == credits_.end() ? 0.0 : found->second;
}

std::optional<IssuedQuote> PaymentProcessor::findQuote(const std::string& reference) const {
    std::lock_guard lock(mutex_);
    const auto found = quotes_.find(reference);
    if (found == quotes_.end()) {
        return std::nullopt;
    }
    return found->second.quote;
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
    for (const auto& item : loaded.verified_until) {
        if (item.expires_at_unix > now_unix) {
            const auto remaining = std::chrono::seconds{item.expires_at_unix - now_unix};
            CachedProof proof;
            proof.expires_at = std::chrono::steady_clock::now() + remaining;
            proof.expires_at_unix = item.expires_at_unix;
            proof.endpoint_path = item.endpoint_path;
            proof.wallet_address = item.wallet_address;
            proof.network = item.network;
            proof.reference = item.reference;
            proof.price_usdc = item.price_usdc;
            verified_until_[item.tx_hash] = std::move(proof);
            verified_cache_.put(item.tx_hash, "verified", std::chrono::duration_cast<std::chrono::seconds>(remaining));
        }
    }
    for (const auto& item : loaded.quotes) {
        if (item.expires_at_unix > now_unix && !item.reference.empty()) {
            quotes_[item.reference] =
                OpenQuote{item, std::chrono::steady_clock::now() + std::chrono::seconds{item.expires_at_unix - now_unix}};
        }
    }
    for (const auto& item : loaded.credits) {
        credits_[gateway::blockchain::normalizeAddress(item.wallet_address)] = item.balance_usdc;
    }
    for (const auto& item : loaded.authorizations) {
        if (!item.nonce.empty() && !item.tx_hash.empty()) {
            authorization_txs_[item.nonce] = item.tx_hash;
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
    state.verified_until.reserve(verified_until_.size());
    for (const auto& item : verified_until_) {
        VerifiedPaymentRecord record;
        record.tx_hash = item.first;
        record.expires_at_unix = item.second.expires_at_unix;
        record.endpoint_path = item.second.endpoint_path;
        record.wallet_address = item.second.wallet_address;
        record.network = item.second.network;
        record.reference = item.second.reference;
        record.price_usdc = item.second.price_usdc;
        state.verified_until.push_back(std::move(record));
    }
    state.quotes.reserve(quotes_.size());
    for (const auto& item : quotes_) {
        state.quotes.push_back(item.second.quote);
    }
    for (const auto& item : credits_) {
        state.credits.push_back(PrepaidBalance{item.first, item.second});
    }
    for (const auto& item : authorization_txs_) {
        state.authorizations.push_back(SubmittedAuthorization{item.first, item.second});
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
