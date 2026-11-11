#pragma once

#include "gateway/core/Configuration.h"

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus {
class Registry;
class Exposer;
template <typename T>
class Family;
class Counter;
class Histogram;
class Gauge;
}  // namespace prometheus

namespace gateway::monitoring {

class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    void initialize(std::uint16_t port);

    void recordRequest(const std::string& endpoint, const std::string& status,
                       std::chrono::microseconds duration);
    void recordPayment(gateway::core::Network network, double amount_usdc, bool verified);
    void recordVerificationLatency(const std::string& stage, std::chrono::microseconds duration);
    void recordBlockchainQuery(gateway::core::Network network, bool success,
                               std::chrono::microseconds duration);
    void setNetworkHealth(gateway::core::Network network, bool healthy);
    void setActiveConnections(double connections);
    void recordCacheHit(const std::string& cache_type);
    void recordCacheMiss(const std::string& cache_type);
    void recordError(const std::string& error_type);

    [[nodiscard]] double requestTotal(const std::string& endpoint, const std::string& status);
    [[nodiscard]] double networkHealth(gateway::core::Network network);

private:
    std::shared_ptr<prometheus::Registry> registry_;
    std::unique_ptr<prometheus::Exposer> exposer_;
    prometheus::Family<prometheus::Counter>* request_counter_ = nullptr;
    prometheus::Family<prometheus::Counter>* payment_counter_ = nullptr;
    prometheus::Family<prometheus::Counter>* payment_amount_ = nullptr;
    prometheus::Family<prometheus::Counter>* error_counter_ = nullptr;
    prometheus::Family<prometheus::Counter>* cache_hits_ = nullptr;
    prometheus::Family<prometheus::Counter>* cache_misses_ = nullptr;
    prometheus::Family<prometheus::Counter>* blockchain_queries_ = nullptr;
    prometheus::Family<prometheus::Histogram>* request_duration_ = nullptr;
    prometheus::Family<prometheus::Histogram>* verification_duration_ = nullptr;
    prometheus::Family<prometheus::Histogram>* blockchain_duration_ = nullptr;
    prometheus::Family<prometheus::Gauge>* network_health_ = nullptr;
    prometheus::Family<prometheus::Gauge>* active_connections_ = nullptr;
};

struct HealthReport {
    int status_code = 503;
    std::string body;
};

[[nodiscard]] HealthReport makeHealthReport(const std::vector<std::pair<std::string, bool>>& networks);

class Logger {
public:
    static void initialize(const std::string& log_level);
    static void initialize(const std::string& log_level, std::ostream& capture);

    static void trace(std::string_view message);
    static void debug(std::string_view message);
    static void info(std::string_view message);
    static void warn(std::string_view message);
    static void error(std::string_view message);

    static void logRequest(const std::string& request_id,
                           const std::string& wallet_address,
                           const std::string& endpoint,
                           double payment_amount,
                           double reputation_score,
                           int response_status,
                           std::chrono::microseconds duration);
    static void logSecurityEvent(const std::string& event_type,
                                 const std::string& wallet_address,
                                 const std::string& reason);
};

}  // namespace gateway::monitoring
