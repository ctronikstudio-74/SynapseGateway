#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace gateway::blockchain {

[[nodiscard]] std::vector<std::uint8_t> rlpBytes(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> rlpUint(std::uint64_t value);
[[nodiscard]] std::vector<std::uint8_t> rlpList(const std::vector<std::vector<std::uint8_t>>& items);

}  // namespace gateway::blockchain
