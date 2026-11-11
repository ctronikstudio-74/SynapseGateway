#pragma once

#include "gateway/blockchain/blockchain_query.h"
#include "gateway/core/IdentityVerifier.h"
#include "gateway/core/PaymentProcessor.h"
#include "gateway/core/ReputationChecker.h"
#include "gateway/core/backend_client.h"
#include "gateway/monitoring/MetricsCollector.h"
#include "gateway/utils/rate_limiter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace gateway::core {

struct GatewayResponse {
    int status = 500;
    std::string body;
    std::string content_type = "application/json";
    std::vector<HttpHeader> headers;
};

class GatewayRouter {
public:
    GatewayRouter(std::shared_ptr<Configuration> config,
                  std::shared_ptr<PaymentProcessor> payment,
                  std::shared_ptr<IdentityVerifier> identity,
                  std::shared_ptr<ReputationChecker> reputation,
                  std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                  std::shared_ptr<BackendClient> backend,
                  std::shared_ptr<gateway::monitoring::MetricsCollector> metrics = nullptr);

    [[nodiscard]] GatewayResponse handle(const ForwardRequest& request);

    void applyConfiguration(std::shared_ptr<Configuration> config);
    void shutdown(std::chrono::seconds drain_timeout);
    void setTimeSource(std::function<std::chrono::steady_clock::time_point()> clock);

    [[nodiscard]] std::uint64_t inFlight() const;
    [[nodiscard]] bool isAccepting() const;

private:
    [[nodiscard]] std::shared_ptr<Configuration> configuration() const;
    [[nodiscard]] std::chrono::steady_clock::time_point now() const;
    [[nodiscard]] GatewayResponse json(int status, std::string body) const;

    std::shared_ptr<Configuration> config_;
    std::shared_ptr<PaymentProcessor> payment_;
    std::shared_ptr<IdentityVerifier> identity_;
    std::shared_ptr<ReputationChecker> reputation_;
    std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain_;
    std::shared_ptr<BackendClient> backend_;
    std::shared_ptr<gateway::monitoring::MetricsCollector> metrics_;
    gateway::utils::RateLimiter rate_limiter_;
    mutable std::mutex config_mutex_;
    std::function<std::chrono::steady_clock::time_point()> clock_;
    std::atomic<bool> accepting_{true};
    std::atomic<std::uint64_t> in_flight_{0};
    std::atomic<std::uint64_t> request_ids_{0};
};

}  // namespace gateway::core
