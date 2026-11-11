#include "gateway/blockchain/network_selection.h"

namespace gateway::blockchain {

std::optional<gateway::core::Network> selectOptimalNetwork(std::span<const NetworkCandidate> candidates) {
    const NetworkCandidate* best = nullptr;
    for (const NetworkCandidate& candidate : candidates) {
        if (!candidate.enabled || !candidate.healthy) {
            continue;
        }
        if (best == nullptr || candidate.gas_fee < best->gas_fee) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return best->network;
}

}  // namespace gateway::blockchain
