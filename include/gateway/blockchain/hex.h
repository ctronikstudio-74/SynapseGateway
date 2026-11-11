#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::blockchain {

[[nodiscard]] std::string toHex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> fromHex(std::string_view hex);
[[nodiscard]] std::string hexToDecimal(std::string_view hex);
[[nodiscard]] std::uint64_t hexToUint64(std::string_view hex);
[[nodiscard]] std::string normalizeAddress(std::string_view address);

}  // namespace gateway::blockchain
