#pragma once

#include "gateway/core/Configuration.h"
#include "gateway/core/backend_client.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::core {

enum class FilterPhase { BeforePayment, AfterIdentity, BeforeForward };

struct FilterContext {
    FilterPhase phase = FilterPhase::BeforePayment;
    const ForwardRequest* request = nullptr;
    std::string wallet;
    std::string path;
    const EndpointConfig* endpoint = nullptr;
    std::vector<HttpHeader> added_headers;
};

struct FilterResult {
    bool continue_request = true;
    int status = 403;
    std::string body;
};

class GatewayFilter {
public:
    virtual ~GatewayFilter() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual FilterResult apply(FilterContext& context) = 0;
};

class FilterChain {
public:
    void add(std::shared_ptr<GatewayFilter> filter) { filters_.push_back(std::move(filter)); }

    [[nodiscard]] FilterResult run(FilterContext& context) const {
        for (const auto& filter : filters_) {
            FilterResult result = filter->apply(context);
            if (!result.continue_request) {
                if (result.body.empty()) {
                    result.body = std::string(R"({"error":"request rejected by filter"})");
                }
                return result;
            }
        }
        return FilterResult{};
    }

    [[nodiscard]] std::size_t size() const { return filters_.size(); }

private:
    std::vector<std::shared_ptr<GatewayFilter>> filters_;
};

}  // namespace gateway::core
