#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace gateway::utils {

/**
 * @brief Thread-safe Bloom Filter for probabilistic set membership testing
 * 
 * Used for replay attack prevention by tracking processed transaction hashes.
 * Provides O(1) insertion and lookup with configurable false positive rate.
 * 
 * Thread-safety: Multiple concurrent readers and writers supported via shared_mutex.
 */
class BloomFilter {
public:
    /**
     * @brief Construct a Bloom Filter with specified parameters
     * 
     * @param size_bits Size of the bit array in bits
     * @param num_hash_functions Number of hash functions to use (typically 3-7)
     * 
     * False positive rate approximation: (1 - e^(-k*n/m))^k
     * where k = num_hash_functions, n = elements inserted, m = size_bits
     */
    explicit BloomFilter(size_t size_bits, size_t num_hash_functions = 5);
    
    /**
     * @brief Add an element to the Bloom Filter
     * 
     * @param element String element to add (typically a transaction hash)
     * 
     * Thread-safe: Multiple concurrent add() calls are synchronized
     */
    void add(const std::string& element);
    
    /**
     * @brief Check if an element might be in the set
     * 
     * @param element String element to check
     * @return true if element might be in the set (possible false positive)
     * @return false if element is definitely not in the set (no false negatives)
     * 
     * Thread-safe: Concurrent with add() and other contains() calls
     */
    bool contains(const std::string& element) const;
    
    /**
     * @brief Clear all elements from the Bloom Filter
     * 
     * Resets the bit array to all zeros.
     * 
     * Thread-safe: Exclusive access during clear operation
     */
    void clear();
    
    /**
     * @brief Get the size of the Bloom Filter in bits
     * 
     * @return size_t Number of bits in the bit array
     */
    size_t size_bits() const noexcept { return size_bits_; }
    
    /**
     * @brief Get the number of hash functions used
     * 
     * @return size_t Number of hash functions
     */
    size_t num_hash_functions() const noexcept { return num_hash_functions_; }
    
    /**
     * @brief Get approximate number of elements added (not guaranteed accurate)
     * 
     * @return size_t Approximate count of add() calls
     */
    size_t approximate_count() const noexcept { return element_count_.load(); }
    
    /**
     * @brief Calculate theoretical false positive rate
     * 
     * @return double False positive probability (0.0 - 1.0)
     * 
     * Formula: (1 - e^(-k*n/m))^k
     * where k = num_hash_functions, n = approximate_count, m = size_bits
     */
    double false_positive_rate() const;
    
private:
    /**
     * @brief Generate k hash values for an element using double hashing
     * 
     * Uses two primary hash functions (MurmurHash and FNV-1a) to generate
     * k independent hash values via double hashing: h(i) = h1(x) + i*h2(x)
     * 
     * @param element Element to hash
     * @return std::vector<size_t> k hash values modulo size_bits
     */
    std::vector<size_t> hash(const std::string& element) const;
    
    /**
     * @brief MurmurHash3 32-bit hash function
     * 
     * @param data Input data
     * @param len Length of input data
     * @param seed Hash seed
     * @return uint32_t Hash value
     */
    static uint32_t murmur_hash3(const void* data, size_t len, uint32_t seed);
    
    /**
     * @brief FNV-1a hash function
     * 
     * @param data Input data
     * @param len Length of input data
     * @return uint32_t Hash value
     */
    static uint32_t fnv1a_hash(const void* data, size_t len);
    
    /**
     * @brief Set a bit in the bit array (thread-safe)
     * 
     * @param index Bit index to set
     */
    void set_bit(size_t index);
    
    /**
     * @brief Check if a bit is set in the bit array (thread-safe)
     * 
     * @param index Bit index to check
     * @return true if bit is set
     */
    bool get_bit(size_t index) const;
    
    size_t size_bits_;               // Number of bits in the bit array
    size_t num_hash_functions_;      // Number of hash functions to use
    
    // Bit array stored as bytes (8 bits per byte)
    std::vector<uint8_t> bit_array_;
    
    // Approximate count of elements added (for false positive rate calculation)
    mutable std::atomic<size_t> element_count_{0};
    
    // Mutex for thread-safe operations
    mutable std::shared_mutex mutex_;
};

} // namespace gateway::utils
