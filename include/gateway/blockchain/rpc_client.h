#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace gateway::blockchain {

class RpcClient {
public:
    virtual ~RpcClient() = default;

    /**
     * @brief Execute one JSON-RPC call and return the `result` value.
     *
     * Throws std::runtime_error when the URL is invalid, the node returns an
     * error object, or the transport fails. Timeout is 5 seconds with one retry.
     */
    virtual nlohmann::json call(const std::string& rpc_url,
                                const std::string& method,
                                const nlohmann::json& params) = 0;
};

class HttpRpcClient final : public RpcClient {
public:
    explicit HttpRpcClient(std::size_t pool_size = 8);

    nlohmann::json call(const std::string& rpc_url,
                        const std::string& method,
                        const nlohmann::json& params) override;

private:
    std::size_t pool_size_;
};

}  // namespace gateway::blockchain
