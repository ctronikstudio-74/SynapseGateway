#pragma once

#include "gateway/blockchain/hex.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::core {

[[nodiscard]] inline bool walletListed(const std::vector<std::string>& addresses, std::string_view wallet) {
    const std::string normalized = gateway::blockchain::normalizeAddress(wallet);
    for (const std::string& address : addresses) {
        if (gateway::blockchain::normalizeAddress(address) == normalized) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool nftMeetsMinimumAge(std::uint64_t registration_timestamp,
                                             std::uint64_t minimum_age_seconds,
                                             std::uint64_t now_unix) {
    if (minimum_age_seconds == 0) {
        return true;
    }
    if (registration_timestamp == 0 || now_unix < registration_timestamp) {
        return false;
    }
    return now_unix - registration_timestamp >= minimum_age_seconds;
}

}  // namespace gateway::core
