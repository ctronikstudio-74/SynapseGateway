#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace synapse::gateway {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse exchange(const std::string& method,
                                  const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

[[nodiscard]] std::shared_ptr<Transport> makeHttpTransport();

struct Quote {
    double price_usdc = 0.0;
    double balance_usdc = 0.0;
    std::string payment_wallet;
    std::string network;
    std::string transaction_reference;
    std::uint64_t expires_at_unix = 0;
    std::string top_up;
};

struct Payment {
    std::string tx_hash;
    std::string authorization_json;
};

struct CallResult {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<Quote> quote;

    [[nodiscard]] std::string header(const std::string& name) const;
};

struct CallOptions {
    std::string tx_hash;
    std::string authorization_json;
    std::string proof;
    std::string reference;
    std::uint64_t expiry_unix = 0;
    std::uint64_t chain_id = 0;
};

class Client {
public:
    Client(std::string base_url,
           std::string private_key_hex,
           std::string verifying_contract,
           std::shared_ptr<Transport> transport,
           std::uint64_t chain_id = 8453);

    [[nodiscard]] CallResult health() const;
    [[nodiscard]] CallResult catalog() const;
    [[nodiscard]] CallResult mcp(const std::string& method,
                                 const std::string& params_json = "{}",
                                 const std::string& id = "1") const;
    [[nodiscard]] CallResult call(const std::string& method,
                                  const std::string& path,
                                  const std::string& body = {},
                                  const CallOptions& options = {}) const;
    [[nodiscard]] CallResult invoke(const std::string& method,
                                    const std::string& path,
                                    const std::string& body,
                                    const std::function<Payment(const Quote&)>& payer) const;
    [[nodiscard]] CallResult deposit(const std::string& tx_hash) const;
    [[nodiscard]] CallResult refund(const std::string& tx_hash,
                                    const std::string& wallet_address,
                                    double gas_fee_usdc) const;
    [[nodiscard]] CallResult decide(const std::string& original_method,
                                    const std::string& original_path,
                                    const std::string& body,
                                    const std::string& proxy) const;

    [[nodiscard]] const std::string& wallet() const;
    [[nodiscard]] const std::string& verifyingContract() const;

private:
    [[nodiscard]] CallResult exchange(const std::string& method,
                                      const std::string& path,
                                      const std::string& body,
                                      const std::vector<std::pair<std::string, std::string>>& headers,
                                      bool signed_call,
                                      const CallOptions& options) const;

    std::string base_url_;
    std::string private_key_hex_;
    std::string wallet_;
    std::string verifying_contract_;
    std::shared_ptr<Transport> transport_;
    std::uint64_t chain_id_;
};

[[nodiscard]] std::uint64_t chainIdForNetwork(const std::string& network);

}  // namespace synapse::gateway
