#pragma once

#include "gateway/core/header_policy.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace gateway::core {

struct ForwardRequest {
    std::string method;
    std::string target;
    std::string body;
    std::vector<HttpHeader> headers;
};

struct ForwardResult {
    enum class Kind { Ok, ConnectionFailed, Timeout };

    Kind kind = Kind::ConnectionFailed;
    int status = 502;
    std::string body;
    std::vector<HttpHeader> headers;
};

class BackendClient {
public:
    virtual ~BackendClient() = default;

    virtual ForwardResult forward(const std::string& backend_url,
                                  const ForwardRequest& request,
                                  std::chrono::milliseconds connect_timeout,
                                  std::chrono::milliseconds read_timeout) = 0;

    [[nodiscard]] virtual std::size_t poolCapacity(const std::string& backend_url) const = 0;
    [[nodiscard]] virtual std::uint64_t reusedConnections() const = 0;
};

class BackendConnectionPool final : public BackendClient {
public:
    static constexpr std::size_t kMinimumConnections = 10;

    explicit BackendConnectionPool(std::size_t min_connections_per_backend = kMinimumConnections);

    ForwardResult forward(const std::string& backend_url,
                          const ForwardRequest& request,
                          std::chrono::milliseconds connect_timeout,
                          std::chrono::milliseconds read_timeout) override;

    [[nodiscard]] std::size_t poolCapacity(const std::string& backend_url) const override;
    [[nodiscard]] std::uint64_t reusedConnections() const override;

private:
    std::size_t min_connections_;
};

}  // namespace gateway::core
