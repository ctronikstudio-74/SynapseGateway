#include "gateway/core/IdentityVerifier.h"

#include "gateway/core/Configuration.h"
#include "gateway/core/validation_rules.h"

#include <cctype>
#include <string_view>

namespace gateway::core {
namespace {

std::string lower(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string cacheKey(Network network, const std::string& wallet) {
    return NetworkToString(network) + ":" + lower(wallet);
}

}  // namespace

IdentityVerifier::IdentityVerifier(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                                   std::size_t cache_entries)
    : blockchain_(std::move(blockchain)), cache_(cache_entries, kCacheTtl) {}

IdentityVerificationResult IdentityVerifier::verifyIdentity(const std::string& wallet_address, Network network) {
    IdentityVerificationResult result;
    if (!blockchain_->supportsNetwork(network)) {
        result.status = IdentityVerificationResult::Status::INVALID_NETWORK;
        result.message = "identity registry is not configured for this network";
        return result;
    }
    const std::string key = cacheKey(network, wallet_address);
    if (auto cached = cache_.get(key)) {
        result.status = IdentityVerificationResult::Status::VERIFIED;
        result.message = "cached identity";
        result.identity_data = std::move(*cached);
        return result;
    }

    const auto lookup = blockchain_->getIdentityNFT(wallet_address, network);
    if (lookup.status == gateway::blockchain::LookupStatus::NotFound || !lookup.nft) {
        result.status = lookup.status == gateway::blockchain::LookupStatus::Error
                            ? IdentityVerificationResult::Status::VERIFICATION_ERROR
                            : IdentityVerificationResult::Status::NO_IDENTITY_NFT;
        result.message = result.status == IdentityVerificationResult::Status::VERIFICATION_ERROR
                             ? "identity registry query failed"
                             : "wallet does not hold an identity NFT";
        return result;
    }
    cache_.put(key, *lookup.nft, kCacheTtl);
    result.status = IdentityVerificationResult::Status::VERIFIED;
    result.message = "identity verified";
    result.identity_data = lookup.nft;
    return result;
}

std::optional<std::string> IdentityVerifier::extractWalletAddress(const WalletSource& source) {
    static constexpr std::string_view kHeaders[] = {"x-agent-wallet", "x-wallet-address", "x-payment-wallet"};
    for (const auto& [name, value] : source.headers) {
        const std::string header = lower(name);
        const bool known = header == kHeaders[0] || header == kHeaders[1] || header == kHeaders[2];
        if (known && isValidAddress(value)) {
            return value;
        }
    }
    if (source.transaction_from && isValidAddress(*source.transaction_from)) {
        return *source.transaction_from;
    }
    return std::nullopt;
}

}  // namespace gateway::core
