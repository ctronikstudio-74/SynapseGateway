#include "gateway/monitoring/MetricsCollector.h"

#include <gtest/gtest.h>

#include <sstream>

TEST(MetricsCollectorTest, RecordsRequestsAndNetworkHealth) {
    gateway::monitoring::MetricsCollector metrics;
    metrics.initialize(0);
    metrics.recordRequest("/api/v1/chat", "200", std::chrono::microseconds{1500});
    metrics.recordPayment(gateway::core::Network::BASE, 0.001, true);
    metrics.recordVerificationLatency("payment", std::chrono::microseconds{800});
    metrics.recordBlockchainQuery(gateway::core::Network::BASE, true, std::chrono::microseconds{400});
    metrics.recordCacheHit("identity");
    metrics.recordCacheMiss("reputation");
    metrics.recordError("replay_attack_detected");
    metrics.setNetworkHealth(gateway::core::Network::BASE, true);
    metrics.setActiveConnections(3);
    EXPECT_DOUBLE_EQ(metrics.requestTotal("/api/v1/chat", "200"), 1.0);
    EXPECT_DOUBLE_EQ(metrics.networkHealth(gateway::core::Network::BASE), 1.0);
}

TEST(HealthReportTest, Returns503WhenAnyNetworkIsDown) {
    const auto healthy = gateway::monitoring::makeHealthReport({{"base", true}, {"arbitrum", true}});
    EXPECT_EQ(healthy.status_code, 200);
    EXPECT_NE(healthy.body.find("\"ok\""), std::string::npos);

    const auto degraded = gateway::monitoring::makeHealthReport({{"base", false}});
    EXPECT_EQ(degraded.status_code, 503);
    const auto empty = gateway::monitoring::makeHealthReport({});
    EXPECT_EQ(empty.status_code, 503);
}

TEST(LoggerTest, EmitsJsonRequestAndSecurityEvents) {
    std::ostringstream capture;
    gateway::monitoring::Logger::initialize("info", capture);
    gateway::monitoring::Logger::logRequest("req-1", "0xabc", "/api/v1/chat", 0.001, 0.8, 200,
                                            std::chrono::microseconds{1200});
    gateway::monitoring::Logger::logSecurityEvent("replay_attack", "0xabc", "duplicate transaction");
    const std::string text = capture.str();
    EXPECT_NE(text.find("\"event\":\"request\""), std::string::npos);
    EXPECT_NE(text.find("\"wallet_address\":\"0xabc\""), std::string::npos);
    EXPECT_NE(text.find("\"event_type\":\"replay_attack\""), std::string::npos);
}
