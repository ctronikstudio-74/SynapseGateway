#include "gateway/utils/bloom_filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gateway::utils {

BloomFilter::BloomFilter(size_t size_bits, size_t num_hash_functions)
    : size_bits_(size_bits)
    , num_hash_functions_(num_hash_functions)
    , bit_array_((size_bits + 7) / 8, 0)  // Round up to nearest byte
{
    if (size_bits == 0) {
        throw std::invalid_argument("Bloom filter size must be greater than 0");
    }
    if (num_hash_functions == 0) {
        throw std::invalid_argument("Number of hash functions must be greater than 0");
    }
}

void BloomFilter::add(const std::string& element) {
    auto hashes = hash(element);
    
    // Acquire exclusive lock for writing
    std::unique_lock lock(mutex_);
    
    for (size_t hash_value : hashes) {
        set_bit(hash_value);
    }
    
    element_count_.fetch_add(1, std::memory_order_relaxed);
}

bool BloomFilter::contains(const std::string& element) const {
    auto hashes = hash(element);
    
    // Acquire shared lock for reading
    std::shared_lock lock(mutex_);
    
    for (size_t hash_value : hashes) {
        if (!get_bit(hash_value)) {
            return false;  // Definitely not in the set
        }
    }
    
    return true;  // Might be in the set (possible false positive)
}

void BloomFilter::clear() {
    // Acquire exclusive lock for clearing
    std::unique_lock lock(mutex_);
    
    std::fill(bit_array_.begin(), bit_array_.end(), 0);
    element_count_.store(0, std::memory_order_relaxed);
}

double BloomFilter::false_positive_rate() const {
    size_t n = element_count_.load(std::memory_order_relaxed);
    if (n == 0) {
        return 0.0;
    }
    
    double m = static_cast<double>(size_bits_);
    double k = static_cast<double>(num_hash_functions_);
    
    // Formula: (1 - e^(-k*n/m))^k
    double exp_term = std::exp(-k * n / m);
    return std::pow(1.0 - exp_term, k);
}

std::vector<size_t> BloomFilter::hash(const std::string& element) const {
    std::vector<size_t> hashes;
    hashes.reserve(num_hash_functions_);
    
    // Generate two primary hash values
    uint32_t h1 = murmur_hash3(element.data(), element.size(), 0x9747b28c);
    uint32_t h2 = fnv1a_hash(element.data(), element.size());
    
    // Use double hashing to generate k independent hash values
    // h(i) = (h1 + i * h2) mod m
    for (size_t i = 0; i < num_hash_functions_; ++i) {
        uint64_t combined = h1 + (i * static_cast<uint64_t>(h2));
        hashes.push_back(combined % size_bits_);
    }
    
    return hashes;
}

uint32_t BloomFilter::murmur_hash3(const void* data, size_t len, uint32_t seed) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const size_t nblocks = len / 4;
    
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    // Body - process 4-byte blocks
    for (size_t i = 0; i < nblocks; ++i) {
        uint32_t k1;
        std::memcpy(&k1, bytes + i * 4, sizeof(k1));
        
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);  // rotl32(k1, 15)
        k1 *= c2;
        
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);  // rotl32(h1, 13)
        h1 = h1 * 5 + 0xe6546b64;
    }
    
    // Tail - process remaining bytes
    const uint8_t* tail = bytes + nblocks * 4;
    uint32_t k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; [[fallthrough]];
        case 2: k1 ^= tail[1] << 8;  [[fallthrough]];
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << 15) | (k1 >> 17);
                k1 *= c2;
                h1 ^= k1;
    }
    
    // Finalization
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

uint32_t BloomFilter::fnv1a_hash(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 0x811c9dc5;  // FNV offset basis
    const uint32_t fnv_prime = 0x01000193;
    
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= fnv_prime;
    }
    
    return hash;
}

void BloomFilter::set_bit(size_t index) {
    size_t byte_index = index / 8;
    size_t bit_offset = index % 8;
    const auto mask = static_cast<uint8_t>(1u << static_cast<unsigned>(bit_offset));
    bit_array_[byte_index] |= mask;
}

bool BloomFilter::get_bit(size_t index) const {
    size_t byte_index = index / 8;
    size_t bit_offset = index % 8;
    const auto mask = static_cast<uint8_t>(1u << static_cast<unsigned>(bit_offset));
    return (bit_array_[byte_index] & mask) != 0;
}

} // namespace gateway::utils
