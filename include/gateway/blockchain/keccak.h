#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace gateway::blockchain {

[[nodiscard]] std::array<std::uint8_t, 32> keccak256(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 32> keccak256(std::string_view text);

}  // namespace gateway::blockchain
