#include "gateway/monitoring/MetricsCollector.h"

#include <nlohmann/json.hpp>
#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <sstream>
#include <utility>

namespace gateway::monitoring {
namespace {

prometheus::Histogram::BucketBoundaries latencyBuckets() {
    return prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5};
}

void logJson(spdlog::level::level_enum level, const nlohmann::json& payload) {
    spdlog::log(level, "{}", payload.dump());
}

}  // namespace

MetricsCollector::MetricsCollector() : registry_(std::make_shared<prometheus::Registry>()) {
    request_counter_ = &prometheus::BuildCounter()
                            .Name("gateway_requests_total")
                            .Help("HTTP requests processed by the gateway")
                            .Register(*registry_);
    payment_counter_ = &prometheus::BuildCounter()
                            .Name("gateway_payments_total")
                            .Help("Payment verification outcomes")
                            .Register(*registry_);
    payment_amount_ = &prometheus::BuildCounter()
                           .Name("gateway_payment_amount_usdc_total")
                           .Help("USDC amount observed on verified payments")
                           .Register(*registry_);
    error_counter_ = &prometheus::BuildCounter()
                          .Name("gateway_errors_total")
                          .Help("Gateway errors by type")
                          .Register(*registry_);
    cache_hits_ = &prometheus::BuildCounter()
                       .Name("gateway_cache_hits_total")
                       .Help("Cache hits by cache type")
                       .Register(*registry_);
    cache_misses_ = &prometheus::BuildCounter()
                         .Name("gateway_cache_misses_total")
                         .Help("Cache misses by cache type")
                         .Register(*registry_);
    blockchain_queries_ = &prometheus::BuildCounter()
                               .Name("gateway_blockchain_queries_total")
                               .Help("Blockchain RPC queries")
                               .Register(*registry_);
    request_duration_ = &prometheus::BuildHistogram()
                             .Name("gateway_request_duration_seconds")
                             .Help("End-to-end request duration")
                             .Register(*registry_);
    verification_duration_ = &prometheus::BuildHistogram()
                                  .Name("gateway_verification_duration_seconds")
                                  .Help("Verification stage duration")
                                  .Register(*registry_);
    blockchain_duration_ = &prometheus::BuildHistogram()
                                .Name("gateway_blockchain_duration_seconds")
                                .Help("Blockchain query duration")
                                .Register(*registry_);
    network_health_ = &prometheus::BuildGauge()
                           .Name("gateway_network_healthy")
                           .Help("1 when the network RPC client is healthy")
                           .Register(*registry_);
    active_connections_ = &prometheus::BuildGauge()
                               .Name("gateway_active_connections")
                               .Help("In-flight gateway connections")
                               .Register(*registry_);
}

MetricsCollector::~MetricsCollector() = default;

void MetricsCollector::initialize(std::uint16_t port) {
    if (port == 0) {
        return;
    }
    exposer_ = std::make_unique<prometheus::Exposer>("127.0.0.1:" + std::to_string(port));
    exposer_->RegisterCollectable(registry_);
}

void MetricsCollector::recordRequest(const std::string& endpoint, const std::string& status,
                                     std::chrono::microseconds duration) {
    request_counter_->Add({{"endpoint", endpoint}, {"status", status}}).Increment();
    request_duration_->Add({{"endpoint", endpoint}}, latencyBuckets())
        .Observe(std::chrono::duration<double>(duration).count());
}

void MetricsCollector::recordPayment(gateway::core::Network network, double amount_usdc, bool verified) {
    const std::string name = gateway::core::NetworkToString(network);
    const std::string flag = verified ? "true" : "false";
    payment_counter_->Add({{"network", name}, {"verified", flag}}).Increment();
    if (verified && amount_usdc > 0.0) {
        payment_amount_->Add({{"network", name}}).Increment(amount_usdc);
    }
}

void MetricsCollector::recordVerificationLatency(const std::string& stage, std::chrono::microseconds duration) {
    verification_duration_->Add({{"stage", stage}}, latencyBuckets())
        .Observe(std::chrono::duration<double>(duration).count());
}

void MetricsCollector::recordBlockchainQuery(gateway::core::Network network, bool success,
                                             std::chrono::microseconds duration) {
    const std::string name = gateway::core::NetworkToString(network);
    blockchain_queries_->Add({{"network", name}, {"success", success ? "true" : "false"}}).Increment();
    blockchain_duration_->Add({{"network", name}}, latencyBuckets())
        .Observe(std::chrono::duration<double>(duration).count());
}

void MetricsCollector::setNetworkHealth(gateway::core::Network network, bool healthy) {
    network_health_->Add({{"network", gateway::core::NetworkToString(network)}}).Set(healthy ? 1.0 : 0.0);
}

void MetricsCollector::setActiveConnections(double connections) {
    active_connections_->Add({}).Set(connections);
}

void MetricsCollector::recordCacheHit(const std::string& cache_type) {
    cache_hits_->Add({{"cache_type", cache_type}}).Increment();
}

void MetricsCollector::recordCacheMiss(const std::string& cache_type) {
    cache_misses_->Add({{"cache_type", cache_type}}).Increment();
}

void MetricsCollector::recordError(const std::string& error_type) {
    error_counter_->Add({{"error_type", error_type}}).Increment();
}

double MetricsCollector::requestTotal(const std::string& endpoint, const std::string& status) {
    return request_counter_->Add({{"endpoint", endpoint}, {"status", status}}).Value();
}

double MetricsCollector::networkHealth(gateway::core::Network network) {
    return network_health_->Add({{"network", gateway::core::NetworkToString(network)}}).Value();
}

HealthReport makeHealthReport(const std::vector<std::pair<std::string, bool>>& networks) {
    nlohmann::json body;
    bool healthy = !networks.empty();
    nlohmann::json listed = nlohmann::json::array();
    for (const auto& [name, ok] : networks) {
        listed.push_back({{"name", name}, {"healthy", ok}});
        healthy = healthy && ok;
    }
    body["status"] = healthy ? "ok" : "degraded";
    body["networks"] = std::move(listed);
    HealthReport report;
    report.status_code = healthy ? 200 : 503;
    report.body = body.dump();
    return report;
}

void Logger::initialize(const std::string& log_level) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("gateway", sink);
    logger->set_level(spdlog::level::from_str(log_level));
    logger->set_pattern("%v");
    spdlog::set_default_logger(logger);
}

void Logger::initialize(const std::string& log_level, std::ostream& capture) {
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(capture);
    auto logger = std::make_shared<spdlog::logger>("gateway", sink);
    logger->set_level(spdlog::level::from_str(log_level));
    logger->set_pattern("%v");
    spdlog::set_default_logger(logger);
}

void Logger::trace(std::string_view message) { spdlog::trace("{}", message); }
void Logger::debug(std::string_view message) { spdlog::debug("{}", message); }
void Logger::info(std::string_view message) { spdlog::info("{}", message); }
void Logger::warn(std::string_view message) { spdlog::warn("{}", message); }
void Logger::error(std::string_view message) { spdlog::error("{}", message); }

void Logger::logRequest(const std::string& request_id,
                        const std::string& wallet_address,
                        const std::string& endpoint,
                        double payment_amount,
                        double reputation_score,
                        int response_status,
                        std::chrono::microseconds duration) {
    const nlohmann::json payload = {{"event", "request"},
                                    {"request_id", request_id},
                                    {"wallet_address", wallet_address},
                                    {"endpoint", endpoint},
                                    {"payment_amount", payment_amount},
                                    {"reputation_score", reputation_score},
                                    {"response_status", response_status},
                                    {"duration_us", duration.count()}};
    logJson(spdlog::level::info, payload);
}

void Logger::logSecurityEvent(const std::string& event_type,
                              const std::string& wallet_address,
                              const std::string& reason) {
    const nlohmann::json payload = {{"event", "security"},
                                    {"event_type", event_type},
                                    {"wallet_address", wallet_address},
                                    {"reason", reason}};
    logJson(spdlog::level::warn, payload);
}

}  // namespace gateway::monitoring
