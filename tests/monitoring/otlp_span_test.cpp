#include "gateway/monitoring/otlp_span.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

TEST(OtlpSpanTest, JsonCarriesTraceIdentityAndStatus) {
    gateway::monitoring::SpanRecord record;
    record.method = "GET";
    record.path = "/api/free";
    record.status = 200;
    record.duration = std::chrono::microseconds{15};
    const nlohmann::json body = nlohmann::json::parse(gateway::monitoring::otlpTraceJson(record));
    const auto& span = body.at("resourceSpans").at(0).at("scopeSpans").at(0).at("spans").at(0);
    EXPECT_EQ(span.at("traceId").get<std::string>().size(), 32U);
    EXPECT_EQ(span.at("spanId").get<std::string>().size(), 16U);
    EXPECT_EQ(span.at("name").get<std::string>(), "gateway.request");
    EXPECT_EQ(span.at("attributes").at(2).at("value").at("intValue").get<std::string>(), "200");
    EXPECT_EQ(body.at("resourceSpans").at(0).at("resource").at("attributes").at(0).at("value").at("stringValue"),
              "SynapseGateway");
}
