#include "gateway/blockchain/create2.h"

#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"

#include <string>
#include <vector>

namespace gateway::blockchain {

std::string generateDeterministicWallet(std::string_view seed, gateway::core::Network) {
    const auto salt = keccak256(seed);
    const std::string deployer_material = std::string("gateway-deployer:") + std::string(seed);
    const auto deployer_hash = keccak256(deployer_material);
    const std::string init_material = std::string("gateway-init-v1:") + std::string(seed);
    const auto init_hash = keccak256(init_material);

    std::vector<std::uint8_t> payload;
    payload.reserve(1 + 20 + 32 + 32);
    payload.push_back(0xff);
    payload.insert(payload.end(), deployer_hash.end() - 20, deployer_hash.end());
    payload.insert(payload.end(), salt.begin(), salt.end());
    payload.insert(payload.end(), init_hash.begin(), init_hash.end());

    const auto digest = keccak256(payload);
    const auto address = toHex(std::span<const std::uint8_t>(digest.data() + 12, 20));
    return "0x" + address;
}

}  // namespace gateway::blockchain
