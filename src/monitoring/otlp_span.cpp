#include "gateway/monitoring/otlp_span.h"

#include "gateway/core/header_policy.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <random>
#include <stdexcept>

namespace gateway::monitoring {
namespace {

std::string randomHex(std::size_t bytes) {
    std::random_device device;
    std::uniform_int_distribution<int> distribution(0, 255);
    constexpr char alphabet[] = "0123456789abcdef";
    std::string hex(bytes * 2, '0');
    for (std::size_t index = 0; index < bytes; ++index) {
        const int value = distribution(device);
        hex[index * 2] = alphabet[(value >> 4) & 0x0f];
        hex[index * 2 + 1] = alphabet[value & 0x0f];
    }
    return hex;
}

std::string unixNano(std::chrono::system_clock::time_point point) {
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch()).count();
    return std::to_string(nanos < 0 ? 0 : nanos);
}

}  // namespace

std::string otlpTraceJson(const SpanRecord& record) {
    const auto end = std::chrono::system_clock::now();
    const auto start = end - record.duration;
    nlohmann::json span = {{"traceId", randomHex(16)},
                           {"spanId", randomHex(8)},
                           {"name", "gateway.request"},
                           {"kind", 2},
                           {"startTimeUnixNano", unixNano(start)},
                           {"endTimeUnixNano", unixNano(end)},
                           {"attributes",
                            nlohmann::json::array({{{"key", "http.request.method"}, {"value", {{"stringValue", record.method}}}},
                                                   {{"key", "url.path"}, {"value", {{"stringValue", record.path}}}},
                                                   {{"key", "http.response.status_code"},
                                                    {"value", {{"intValue", std::to_string(record.status)}}}}})},
                           {"status", {{"code", record.status >= 500 ? 2 : 1}}}};
    nlohmann::json body = {{"resourceSpans",
                            nlohmann::json::array({{{"resource",
                                                     {{"attributes",
                                                       nlohmann::json::array({{{"key", "service.name"},
                                                                               {"value", {{"stringValue", "SynapseGateway"}}}}})}}},
                                                    {"scopeSpans", nlohmann::json::array({{{"spans", nlohmann::json::array({span})}}})}})}};
    return body.dump();
}

OtlpHttpExporter::OtlpHttpExporter(std::shared_ptr<gateway::core::BackendClient> backend, std::string endpoint)
    : backend_(std::move(backend)), endpoint_(std::move(endpoint)) {
    if (!backend_ || endpoint_.empty()) {
        throw std::invalid_argument("OTLP export requires a backend client and an endpoint");
    }
}

void OtlpHttpExporter::exportSpan(const SpanRecord& record) {
    gateway::core::ForwardRequest request;
    request.method = "POST";
    request.target = "/v1/traces";
    request.body = otlpTraceJson(record);
    request.headers.push_back({"Content-Type", "application/json"});
    try {
        backend_->forward(endpoint_, request, std::chrono::seconds{1}, std::chrono::seconds{1});
    } catch (const std::exception&) {
    }
}

}  // namespace gateway::monitoring
