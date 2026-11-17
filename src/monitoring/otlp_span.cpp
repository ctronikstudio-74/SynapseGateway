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

nlohmann::json stringAttribute(const char* key, const std::string& value) {
    return nlohmann::json{{"key", key}, {"value", {{"stringValue", value}}}};
}

std::string otlpTraceJson(const SpanRecord& record) {
    const auto end = std::chrono::system_clock::now();
    const auto start = end - record.duration;
    nlohmann::json span;
    span["traceId"] = randomHex(16);
    span["spanId"] = randomHex(8);
    span["name"] = "gateway.request";
    span["kind"] = 2;
    span["startTimeUnixNano"] = unixNano(start);
    span["endTimeUnixNano"] = unixNano(end);
    span["attributes"] = nlohmann::json::array({stringAttribute("http.request.method", record.method),
                                                stringAttribute("url.path", record.path),
                                                nlohmann::json{{"key", "http.response.status_code"},
                                                               {"value", {{"intValue", std::to_string(record.status)}}}}});
    span["status"] = {{"code", record.status >= 500 ? 2 : 1}};
    nlohmann::json body;
    body["resourceSpans"] = nlohmann::json::array();
    nlohmann::json resource_span;
    resource_span["resource"]["attributes"] = nlohmann::json::array({stringAttribute("service.name", "SynapseGateway")});
    resource_span["scopeSpans"] = nlohmann::json::array({nlohmann::json{{"spans", nlohmann::json::array({span})}}});
    body["resourceSpans"].push_back(std::move(resource_span));
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
