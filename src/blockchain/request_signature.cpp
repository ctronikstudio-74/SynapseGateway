#include "gateway/blockchain/request_signature.h"

#include "gateway/blockchain/abi_decoder.h"
#include "gateway/blockchain/hex.h"
#include "gateway/blockchain/keccak.h"

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <algorithm>
#include <vector>

namespace gateway::blockchain {
namespace {

struct Big {
    BIGNUM* value = BN_new();
    ~Big() { BN_free(value); }
    Big() = default;
    Big(const Big&) = delete;
    Big& operator=(const Big&) = delete;
};

struct Group {
    EC_GROUP* value = EC_GROUP_new_by_curve_name(NID_secp256k1);
    ~Group() { EC_GROUP_free(value); }
};

struct Point {
    EC_POINT* value = nullptr;
    explicit Point(const EC_GROUP* group) : value(EC_POINT_new(group)) {}
    ~Point() { EC_POINT_free(value); }
};

struct Context {
    BN_CTX* value = BN_CTX_new();
    ~Context() { BN_CTX_free(value); }
};

struct Key {
    EC_KEY* value = EC_KEY_new_by_curve_name(NID_secp256k1);
    ~Key() { EC_KEY_free(value); }
};

void appendWord(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 32>& word) {
    out.insert(out.end(), word.begin(), word.end());
}

bool loadPrivateKey(std::string_view hex_key, Big& key) {
    const auto bytes = fromHex(hex_key);
    if (bytes.empty() || bytes.size() > 32 || key.value == nullptr) {
        return false;
    }
    std::array<std::uint8_t, 32> padded{};
    std::copy(bytes.begin(), bytes.end(), padded.end() - static_cast<std::ptrdiff_t>(bytes.size()));
    return BN_bin2bn(padded.data(), 32, key.value) != nullptr && !BN_is_zero(key.value);
}

std::string addressFromPoint(const EC_GROUP* group, const EC_POINT* point, BN_CTX* ctx) {
    std::uint8_t encoded[65] = {};
    if (EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, encoded, sizeof(encoded), ctx) != 65) {
        return {};
    }
    const auto hash = keccak256(std::span<const std::uint8_t>(encoded + 1, 64));
    return "0x" + toHex(std::span<const std::uint8_t>(hash.data() + 12, 20));
}

std::optional<std::string> publicAddress(const Big& private_key) {
    Group group;
    Context ctx;
    Point point(group.value);
    if (group.value == nullptr || ctx.value == nullptr || point.value == nullptr) {
        return std::nullopt;
    }
    if (EC_POINT_mul(group.value, point.value, private_key.value, nullptr, nullptr, ctx.value) != 1) {
        return std::nullopt;
    }
    const std::string address = addressFromPoint(group.value, point.value, ctx.value);
    if (address.empty()) {
        return std::nullopt;
    }
    return address;
}

}  // namespace

std::uint64_t chainIdOf(gateway::core::Network network) {
    switch (network) {
    case gateway::core::Network::ETHEREUM_MAINNET:
        return 1;
    case gateway::core::Network::BASE:
        return 8453;
    case gateway::core::Network::ARBITRUM:
        return 42161;
    case gateway::core::Network::POLYGON:
        return 137;
    case gateway::core::Network::BNB_CHAIN:
        return 56;
    }
    return 0;
}

std::array<std::uint8_t, 32> agentRequestDigest(const AgentRequest& request) {
    const auto type_hash = keccak256(
        "Request(string method,string path,bytes32 bodyHash,address wallet,string quoteRef,uint256 expiry)");
    const auto domain_hash =
        keccak256("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)");
    const auto name_hash = keccak256("AgentGateway");
    const auto version_hash = keccak256("1");
    const auto method_hash = keccak256(request.method);
    const auto path_hash = keccak256(request.path);
    const auto body_hash = keccak256(request.body);
    const auto quote_hash = keccak256(request.quote_reference);

    std::vector<std::uint8_t> domain;
    domain.reserve(5 * 32);
    appendWord(domain, domain_hash);
    appendWord(domain, name_hash);
    appendWord(domain, version_hash);
    appendWord(domain, AbiDecoder::encodeUint256(request.chain_id));
    appendWord(domain, AbiDecoder::encodeAddress(request.verifying_contract));

    std::vector<std::uint8_t> message;
    message.reserve(7 * 32);
    appendWord(message, type_hash);
    appendWord(message, method_hash);
    appendWord(message, path_hash);
    appendWord(message, body_hash);
    appendWord(message, AbiDecoder::encodeAddress(request.wallet));
    appendWord(message, quote_hash);
    appendWord(message, AbiDecoder::encodeUint256(request.expiry_unix));

    const auto domain_separator = keccak256(domain);
    const auto struct_hash = keccak256(message);
    std::vector<std::uint8_t> packed;
    packed.reserve(2 + 64);
    packed.push_back(0x19);
    packed.push_back(0x01);
    packed.insert(packed.end(), domain_separator.begin(), domain_separator.end());
    packed.insert(packed.end(), struct_hash.begin(), struct_hash.end());
    return keccak256(packed);
}

std::optional<std::string> addressFromPrivateKey(std::string_view private_key_hex) {
    Big key;
    if (!loadPrivateKey(private_key_hex, key)) {
        return std::nullopt;
    }
    return publicAddress(key);
}

std::optional<std::string> recoverSigner(const std::array<std::uint8_t, 32>& digest, std::string_view signature_hex) {
    const auto bytes = fromHex(signature_hex);
    if (bytes.size() != 65) {
        return std::nullopt;
    }
    int recovery_id = bytes[64];
    if (recovery_id >= 27) {
        recovery_id -= 27;
    }
    if (recovery_id < 0 || recovery_id > 3) {
        return std::nullopt;
    }

    Group group;
    Context ctx;
    Big order;
    Big x;
    Big r;
    Big s;
    Big hash;
    Big inverse;
    Big negative_hash;
    if (group.value == nullptr || ctx.value == nullptr || EC_GROUP_get_order(group.value, order.value, ctx.value) != 1) {
        return std::nullopt;
    }
    if (BN_bin2bn(bytes.data(), 32, r.value) == nullptr || BN_bin2bn(bytes.data() + 32, 32, s.value) == nullptr ||
        BN_bin2bn(digest.data(), 32, hash.value) == nullptr) {
        return std::nullopt;
    }
    if (BN_is_zero(r.value) || BN_is_zero(s.value) || BN_cmp(r.value, order.value) >= 0 ||
        BN_cmp(s.value, order.value) >= 0) {
        return std::nullopt;
    }
    Big half;
    if (BN_rshift1(half.value, order.value) != 1 || BN_cmp(s.value, half.value) > 0) {
        return std::nullopt;
    }
    BN_copy(x.value, r.value);
    if ((recovery_id & 2) != 0) {
        BN_add(x.value, x.value, order.value);
    }
    std::array<std::uint8_t, 33> compressed{};
    compressed[0] = static_cast<std::uint8_t>(0x02 + (recovery_id & 1));
    if (BN_bn2binpad(x.value, compressed.data() + 1, 32) != 32) {
        return std::nullopt;
    }
    Point point(group.value);
    if (point.value == nullptr ||
        EC_POINT_oct2point(group.value, point.value, compressed.data(), compressed.size(), ctx.value) != 1) {
        return std::nullopt;
    }
    BN_mod(hash.value, hash.value, order.value, ctx.value);
    BIGNUM* inverted = BN_mod_inverse(inverse.value, r.value, order.value, ctx.value);
    if (inverted == nullptr) {
        return std::nullopt;
    }
    if (BN_is_zero(hash.value)) {
        BN_zero(negative_hash.value);
    } else if (BN_sub(negative_hash.value, order.value, hash.value) != 1) {
        return std::nullopt;
    }
    Point sum(group.value);
    Point signer(group.value);
    if (sum.value == nullptr || signer.value == nullptr ||
        EC_POINT_mul(group.value, sum.value, negative_hash.value, point.value, s.value, ctx.value) != 1 ||
        EC_POINT_mul(group.value, signer.value, nullptr, sum.value, inverse.value, ctx.value) != 1) {
        return std::nullopt;
    }
    const std::string address = addressFromPoint(group.value, signer.value, ctx.value);
    if (address.empty()) {
        return std::nullopt;
    }
    return address;
}

std::optional<std::array<std::uint8_t, 65>> signDigest(std::string_view private_key_hex,
                                                                   const std::array<std::uint8_t, 32>& digest) {
    Big private_key;
    if (!loadPrivateKey(private_key_hex, private_key)) {
        return std::nullopt;
    }
    const auto expected = publicAddress(private_key);
    if (!expected) {
        return std::nullopt;
    }
    Key key;
    Group group;
    Context ctx;
    Point public_point(group.value);
    if (key.value == nullptr || group.value == nullptr || public_point.value == nullptr ||
        EC_POINT_mul(group.value, public_point.value, private_key.value, nullptr, nullptr, ctx.value) != 1 ||
        EC_KEY_set_private_key(key.value, private_key.value) != 1 ||
        EC_KEY_set_public_key(key.value, public_point.value) != 1) {
        return std::nullopt;
    }
    ECDSA_SIG* signature = ECDSA_do_sign(digest.data(), static_cast<int>(digest.size()), key.value);
    if (signature == nullptr) {
        return std::nullopt;
    }
    const BIGNUM* raw_r = nullptr;
    const BIGNUM* raw_s = nullptr;
    ECDSA_SIG_get0(signature, &raw_r, &raw_s);
    Big r;
    Big s;
    Big order;
    Big half;
    BN_copy(r.value, raw_r);
    BN_copy(s.value, raw_s);
    ECDSA_SIG_free(signature);
    if (EC_GROUP_get_order(group.value, order.value, ctx.value) != 1 || BN_rshift1(half.value, order.value) != 1) {
        return std::nullopt;
    }
    if (BN_cmp(s.value, half.value) > 0) {
        BN_sub(s.value, order.value, s.value);
    }
    std::array<std::uint8_t, 65> encoded{};
    if (BN_bn2binpad(r.value, encoded.data(), 32) != 32 || BN_bn2binpad(s.value, encoded.data() + 32, 32) != 32) {
        return std::nullopt;
    }
    for (int recovery = 0; recovery < 2; ++recovery) {
        encoded[64] = static_cast<std::uint8_t>(27 + recovery);
        const auto signer = recoverSigner(digest, toHex(encoded));
        if (signer && normalizeAddress(*signer) == normalizeAddress(*expected)) {
            return encoded;
        }
    }
    return std::nullopt;
}

std::optional<std::string> signAgentRequest(std::string_view private_key_hex, const AgentRequest& request) {
    const auto signature = signDigest(private_key_hex, agentRequestDigest(request));
    if (!signature) {
        return std::nullopt;
    }
    return "0x" + toHex(*signature);
}

}  // namespace gateway::blockchain
