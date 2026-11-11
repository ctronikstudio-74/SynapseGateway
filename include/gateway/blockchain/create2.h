#pragma once

#include "gateway/core/Configuration.h"

#include <string>
#include <string_view>

namespace gateway::blockchain {

/**
 * @brief CREATE2 address derived only from the wallet seed.
 *
 * The network argument is accepted so callers can ask per chain, but it is not
 * mixed into the hash. Requirement 3.2: the same seed yields the same address
 * on every supported network.
 */
[[nodiscard]] std::string generateDeterministicWallet(std::string_view seed,
                                                      gateway::core::Network network);

}  // namespace gateway::blockchain
