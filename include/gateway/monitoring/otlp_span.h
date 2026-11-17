#pragma once

#include "gateway/core/backend_client.h"

#include <chrono>
#include <memory>
#include <string>

namespace gateway::monitoring {

struct SpanRecord {
    std::string method;
    std::string path;
    int status = 0;
    std::chrono::microseconds duration{0};
};

[[nodiscard]] std::string otlpTraceJson(const SpanRecord& record);

class SpanExporter {
public:
    virtual ~SpanExporter() = default;
    virtual void exportSpan(const SpanRecord& record) = 0;
};

class OtlpHttpExporter final : public SpanExporter {
public:
    OtlpHttpExporter(std::shared_ptr<gateway::core::BackendClient> backend, std::string endpoint);

    void exportSpan(const SpanRecord& record) override;

private:
    std::shared_ptr<gateway::core::BackendClient> backend_;
    std::string endpoint_;
};

}  // namespace gateway::monitoring
