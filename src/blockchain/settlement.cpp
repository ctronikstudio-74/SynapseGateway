#include "gateway/blockchain/settlement.h"

#include "gateway/blockchain/abi_decoder.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"
#include "gateway/blockchain/request_signature.h"
#include "gateway/blockchain/rlp.h"
#include "gateway/core/validation_rules.h"

#include <algorithm>

namespace gateway::blockchain {
namespace {

void appendWord(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 32>& word) {
    out.insert(out.end(), word.begin(), word.end());
}

std::optional<std::array<std::uint8_t, 32>> uint256FromDecimal(std::string_view digits) {
    if (digits.empty()) {
        return std::array<std::uint8_t, 32>{};
    }
    std::vector<std::uint8_t> little{0};
    for (const char character : digits) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        unsigned carry = static_cast<unsigned>(character - '0');
        for (auto& byte : little) {
            const unsigned next = static_cast<unsigned>(byte) * 10u + carry;
            byte = static_cast<std::uint8_t>(next & 0xffu);
            carry = next >> 8u;
        }
        while (carry > 0) {
            little.push_back(static_cast<std::uint8_t>(carry & 0xffu));
            carry >>= 8u;
        }
    }
    if (little.size() > 32) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 32> word{};
    for (std::size_t index = 0; index < little.size(); ++index) {
        word[31 - index] = little[index];
    }
    return word;
}

std::array<std::uint8_t, 32> bytes32(std::string_view hex_word) {
    auto raw = fromHex(hex_word);
    std::array<std::uint8_t, 32> word{};
    if (raw.size() > 32) {
        raw.erase(raw.begin(), raw.end() - 32);
    }
    if (!raw.empty()) {
        std::copy(raw.begin(), raw.end(), word.end() - static_cast<std::ptrdiff_t>(raw.size()));
    }
    return word;
}

std::vector<std::uint8_t> rlpIntegerWord(const std::array<std::uint8_t, 32>& word) {
    std::size_t start = 0;
    while (start < word.size() && word[start] == 0) {
        ++start;
    }
    if (start == word.size()) {
        return rlpUint(0);
    }
    return rlpBytes(std::span<const std::uint8_t>(word.data() + start, word.size() - start));
}

}  // namespace

std::array<std::uint8_t, 32> transferAuthorizationDigest(const TransferAuthorization& authorization,
                                                       std::string_view domain_name,
                                                       std::string_view domain_version,
                                                       std::uint64_t chain_id,
                                                       std::string_view verifying_contract) {
    const auto type_hash = keccak256(
        "TransferWithAuthorization(address from,address to,uint256 value,uint256 validAfter,uint256 validBefore,bytes32 nonce)");
    const auto domain_type =
        keccak256("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)");
    const auto value = uint256FromDecimal(authorization.value_units).value_or(std::array<std::uint8_t, 32>{});
    std::vector<std::uint8_t> domain;
    appendWord(domain, domain_type);
    appendWord(domain, keccak256(domain_name));
    appendWord(domain, keccak256(domain_version));
    appendWord(domain, AbiDecoder::encodeUint256(chain_id));
    appendWord(domain, AbiDecoder::encodeAddress(verifying_contract));

    std::vector<std::uint8_t> message;
    appendWord(message, type_hash);
    appendWord(message, AbiDecoder::encodeAddress(authorization.from));
    appendWord(message, AbiDecoder::encodeAddress(authorization.to));
    appendWord(message, value);
    appendWord(message, AbiDecoder::encodeUint256(authorization.valid_after));
    appendWord(message, AbiDecoder::encodeUint256(authorization.valid_before));
    appendWord(message, bytes32(authorization.nonce_hex));

    const auto domain_separator = keccak256(domain);
    const auto struct_hash = keccak256(message);
    std::vector<std::uint8_t> packed{0x19, 0x01};
    packed.insert(packed.end(), domain_separator.begin(), domain_separator.end());
    packed.insert(packed.end(), struct_hash.begin(), struct_hash.end());
    return keccak256(packed);
}

std::vector<std::uint8_t> transferAuthorizationCalldata(const TransferAuthorization& authorization) {
    const auto selector = AbiDecoder::functionSelector(
        "transferWithAuthorization(address,address,uint256,uint256,uint256,bytes32,uint8,bytes32,bytes32)");
    const auto signature = fromHex(authorization.signature_hex);
    std::array<std::uint8_t, 32> r{};
    std::array<std::uint8_t, 32> s{};
    std::uint64_t v = 0;
    if (signature.size() == 65) {
        std::copy_n(signature.begin(), 32, r.begin());
        std::copy_n(signature.begin() + 32, 32, s.begin());
        v = signature[64];
    }
    const auto value = uint256FromDecimal(authorization.value_units).value_or(std::array<std::uint8_t, 32>{});
    std::vector<std::uint8_t> data(selector.begin(), selector.end());
    appendWord(data, AbiDecoder::encodeAddress(authorization.from));
    appendWord(data, AbiDecoder::encodeAddress(authorization.to));
    appendWord(data, value);
    appendWord(data, AbiDecoder::encodeUint256(authorization.valid_after));
    appendWord(data, AbiDecoder::encodeUint256(authorization.valid_before));
    appendWord(data, bytes32(authorization.nonce_hex));
    appendWord(data, AbiDecoder::encodeUint256(v));
    appendWord(data, r);
    appendWord(data, s);
    return data;
}

std::vector<std::uint8_t> erc20TransferCalldata(std::string_view to, std::string_view value_units) {
    const auto selector = AbiDecoder::functionSelector("transfer(address,uint256)");
    const auto value = uint256FromDecimal(value_units).value_or(std::array<std::uint8_t, 32>{});
    std::vector<std::uint8_t> data(selector.begin(), selector.end());
    appendWord(data, AbiDecoder::encodeAddress(to));
    appendWord(data, value);
    return data;
}

std::optional<std::vector<std::uint8_t>> signEip1559(const Eip1559Transaction& transaction,
                                                    std::string_view private_key_hex) {
    if (!gateway::core::isValidAddress(transaction.to)) {
        return std::nullopt;
    }
    const auto destination = fromHex(transaction.to);
    const std::vector<std::vector<std::uint8_t>> unsigned_fields = {
        rlpUint(transaction.chain_id),
        rlpUint(transaction.nonce),
        rlpUint(transaction.max_priority_fee),
        rlpUint(transaction.max_fee),
        rlpUint(transaction.gas_limit),
        rlpBytes(destination),
        rlpUint(0),
        rlpBytes(transaction.data),
        rlpList({}),
    };
    auto payload = rlpList(unsigned_fields);
    payload.insert(payload.begin(), 0x02);
    const auto digest = keccak256(payload);
    const auto signature = signDigest(private_key_hex, digest);
    if (!signature) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 32> r{};
    std::array<std::uint8_t, 32> s{};
    std::copy_n(signature->begin(), 32, r.begin());
    std::copy_n(signature->begin() + 32, 32, s.begin());
    const auto parity = static_cast<std::uint64_t>((*signature)[64] >= 27 ? (*signature)[64] - 27 : (*signature)[64]);
    auto signed_fields = unsigned_fields;
    signed_fields.push_back(rlpUint(parity));
    signed_fields.push_back(rlpIntegerWord(r));
    signed_fields.push_back(rlpIntegerWord(s));
    auto raw = rlpList(signed_fields);
    raw.insert(raw.begin(), 0x02);
    return raw;
}

}  // namespace gateway::blockchain
