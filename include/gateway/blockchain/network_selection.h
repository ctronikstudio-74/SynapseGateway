#pragma once

#include "gateway/core/Configuration.h"

#include <optional>
#include <span>

namespace gateway::blockchain {

struct NetworkCandidate {
    gateway::core::Network network = gateway::core::Network::BASE;
    double gas_fee = 0.0;
    bool healthy = false;
    bool enabled = false;
};

/**
 * @brief Lowest gas fee among networks that are both enabled and healthy.
 *
 * Property 2 / Requirement 1.6. Returns nullopt when every candidate is
 * disabled or unhealthy.
 */
[[nodiscard]] std::optional<gateway::core::Network> selectOptimalNetwork(
    std::span<const NetworkCandidate> candidates);

}  // namespace gateway::blockchain
