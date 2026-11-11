#include "gateway/blockchain/keccak.h"

#include <bit>
#include <cstring>
#include <optional>

#include <openssl/evp.h>

namespace gateway::blockchain {
namespace {

static_assert(std::endian::native == std::endian::little, "keccak byte mixing assumes little-endian");

constexpr std::uint64_t rotl64(std::uint64_t value, int bits) {
    return (value << bits) | (value >> (64 - bits));
}

void keccakf(std::uint64_t state[25]) {
    static constexpr std::uint64_t kRoundConstants[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
    static constexpr int kRotations[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                           27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};
    static constexpr int kPiLane[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
                                        15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1};

    for (int round = 0; round < 24; ++round) {
        std::uint64_t bc[5];
        for (int i = 0; i < 5; ++i) {
            bc[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^ state[i + 20];
        }
        for (int i = 0; i < 5; ++i) {
            const std::uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) {
                state[j + i] ^= t;
            }
        }

        std::uint64_t t = state[1];
        for (int i = 0; i < 24; ++i) {
            const int j = kPiLane[i];
            bc[0] = state[j];
            state[j] = rotl64(t, kRotations[i]);
            t = bc[0];
        }

        for (int j = 0; j < 25; j += 5) {
            std::uint64_t tmp[5];
            for (int i = 0; i < 5; ++i) {
                tmp[i] = state[j + i];
            }
            for (int i = 0; i < 5; ++i) {
                state[j + i] = tmp[i] ^ ((~tmp[(i + 1) % 5]) & tmp[(i + 2) % 5]);
            }
        }
        state[0] ^= kRoundConstants[round];
    }
}

void xorBytes(std::uint64_t state[25], const std::uint8_t* data, std::size_t length) {
    auto* raw = reinterpret_cast<std::uint8_t*>(state);
    for (std::size_t i = 0; i < length; ++i) {
        raw[i] ^= data[i];
    }
}

std::array<std::uint8_t, 32> keccak256Internal(std::span<const std::uint8_t> data) {
    constexpr std::size_t kRate = 136;
    std::uint64_t state[25] = {};
    std::size_t offset = 0;
    while (data.size() - offset >= kRate) {
        xorBytes(state, data.data() + offset, kRate);
        keccakf(state);
        offset += kRate;
    }

    std::uint8_t block[kRate] = {};
    const std::size_t remainder = data.size() - offset;
    if (remainder > 0) {
        std::memcpy(block, data.data() + offset, remainder);
    }
    block[remainder] = static_cast<std::uint8_t>(block[remainder] ^ 0x01);
    block[kRate - 1] = static_cast<std::uint8_t>(block[kRate - 1] ^ 0x80);
    xorBytes(state, block, kRate);
    keccakf(state);

    std::array<std::uint8_t, 32> out{};
    std::memcpy(out.data(), state, out.size());
    return out;
}

std::optional<std::array<std::uint8_t, 32>> keccak256OpenSsl(std::span<const std::uint8_t> data) {
    EVP_MD* algorithm = EVP_MD_fetch(nullptr, "KECCAK-256", nullptr);
    if (algorithm == nullptr) {
        return std::nullopt;
    }
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    std::array<std::uint8_t, 32> out{};
    unsigned int length = 0;
    const bool ok = context != nullptr && EVP_DigestInit_ex(context, algorithm, nullptr) == 1 &&
                    EVP_DigestUpdate(context, data.data(), data.size()) == 1 &&
                    EVP_DigestFinal_ex(context, out.data(), &length) == 1 && length == out.size();
    EVP_MD_CTX_free(context);
    EVP_MD_free(algorithm);
    if (!ok) {
        return std::nullopt;
    }
    return out;
}

bool openSslMatchesInternal() {
    const auto openssl_empty = keccak256OpenSsl({});
    if (!openssl_empty) {
        return false;
    }
    return *openssl_empty == keccak256Internal({});
}

}  // namespace

std::array<std::uint8_t, 32> keccak256(std::span<const std::uint8_t> data) {
    static const bool kUseOpenSsl = openSslMatchesInternal();
    if (kUseOpenSsl) {
        if (auto digested = keccak256OpenSsl(data)) {
            return *digested;
        }
    }
    return keccak256Internal(data);
}

std::array<std::uint8_t, 32> keccak256(std::string_view text) {
    return keccak256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                   text.size()));
}

}  // namespace gateway::blockchain
