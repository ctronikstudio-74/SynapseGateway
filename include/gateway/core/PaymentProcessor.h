#pragma once

#include "gateway/blockchain/blockchain_query.h"
#include "gateway/blockchain/models.h"
#include "gateway/core/Configuration.h"
#include "gateway/core/cache.h"
#include "gateway/core/payment_state.h"
#include "gateway/utils/bloom_filter.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gateway::core {

struct PaymentRequest {
    std::string endpoint_path;
    std::string wallet_address;
    std::optional<std::string> tx_hash;
    std::string tx_reference;
    std::uint64_t timestamp_ms = 0;
};

struct PaymentResponse {
    enum class Status {
        NOT_PAID,
        VERIFICATION_PENDING,
        VERIFIED,
        INSUFFICIENT_AMOUNT,
        INVALID_TRANSACTION,
        REPLAY_DETECTED
    };

    Status status = Status::NOT_PAID;
    std::string message;
    std::optional<std::string> payment_wallet;
    std::optional<double> price_usdc;
    std::optional<std::string> preferred_network;
    std::optional<std::string> transaction_reference;
};

[[nodiscard]] bool amountCoversQuote(double paid_usdc, double quoted_usdc);
[[nodiscard]] const EndpointConfig& matchEndpoint(const Configuration& config, const std::string& path);

class PaymentProcessor {
public:
    static constexpr std::chrono::hours kVerifiedPaymentTtl{24};

    PaymentProcessor(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                     std::shared_ptr<Configuration> config,
                     std::filesystem::path refund_store_path);

    [[nodiscard]] PaymentResponse requestPayment(const PaymentRequest& request);
    [[nodiscard]] PaymentResponse verifyPayment(
        const PaymentRequest& request,
        double endpoint_price,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    void markRefundable(const std::string& tx_hash,
                        const std::string& wallet_address,
                        double amount_usdc,
                        const std::string& reason);
    [[nodiscard]] bool processRefund(const std::string& tx_hash,
                                     const std::string& wallet_address,
                                     double gas_fee_usdc);
    [[nodiscard]] std::optional<gateway::models::RefundableTransaction> findRefund(
        const std::string& tx_hash) const;

    void updateConfiguration(std::shared_ptr<Configuration> config);
    void persistState() const;
    [[nodiscard]] std::vector<PendingVerification> pendingVerifications() const;

private:
    struct CachedProof {
        std::chrono::steady_clock::time_point expires_at{};
        std::uint64_t expires_at_unix = 0;
    };

    void loadState();
    void saveStateUnlocked() const;
    void upsertPendingUnlocked(PendingVerification pending);
    [[nodiscard]] std::string nextReference();
    [[nodiscard]] std::shared_ptr<Configuration> configuration() const;

    mutable std::mutex mutex_;

    std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain_;
    std::shared_ptr<Configuration> config_;
    std::filesystem::path refund_store_path_;
    Cache<std::string, std::string> verified_cache_;
    std::unique_ptr<gateway::utils::BloomFilter> replay_filter_;
    std::unordered_map<std::string, CachedProof> verified_until_;
    std::unordered_set<std::string> seen_transactions_;
    std::vector<gateway::models::RefundableTransaction> refunds_;
    std::vector<PendingVerification> pending_;
    std::atomic<std::uint64_t> reference_counter_{0};
};

}  // namespace gateway::core
