#pragma once

#include "gateway/blockchain/blockchain_query.h"
#include "gateway/core/cache.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gateway::core {

struct WalletSource {
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<std::string> transaction_from;
};

struct IdentityVerificationResult {
    enum class Status { VERIFIED, NO_IDENTITY_NFT, INVALID_NETWORK, VERIFICATION_ERROR };

    Status status = Status::VERIFICATION_ERROR;
    std::string message;
    std::optional<gateway::blockchain::IdentityNFT> identity_data;
};

class IdentityVerifier {
public:
    static constexpr std::chrono::seconds kCacheTtl{300};

    IdentityVerifier(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                     std::size_t cache_entries = 1024);

    [[nodiscard]] IdentityVerificationResult verifyIdentity(const std::string& wallet_address,
                                                            Network network);
    [[nodiscard]] static std::optional<std::string> extractWalletAddress(const WalletSource& source);

private:
    std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain_;
    Cache<std::string, gateway::blockchain::IdentityNFT> cache_;
};

}  // namespace gateway::core
