#pragma once

#include "gateway/blockchain/blockchain_query.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gateway::blockchain {

struct Eip1559Transaction {
    std::uint64_t chain_id = 0;
    std::uint64_t nonce = 0;
    std::uint64_t max_priority_fee = 0;
    std::uint64_t max_fee = 0;
    std::uint64_t gas_limit = 0;
    std::string to;
    std::vector<std::uint8_t> data;
};

[[nodiscard]] std::array<std::uint8_t, 32> transferAuthorizationDigest(const TransferAuthorization& authorization,
                                                                      std::string_view domain_name,
                                                                      std::string_view domain_version,
                                                                      std::uint64_t chain_id,
                                                                      std::string_view verifying_contract);

[[nodiscard]] std::vector<std::uint8_t> transferAuthorizationCalldata(const TransferAuthorization& authorization);

[[nodiscard]] std::vector<std::uint8_t> erc20TransferCalldata(std::string_view to, std::string_view value_units);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> signEip1559(const Eip1559Transaction& transaction,
                                                                  std::string_view private_key_hex);

}  // namespace gateway::blockchain
