#include "gateway/utils/bloom_filter.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>
#include <set>

using namespace gateway::utils;

class BloomFilterTest : public ::testing::Test {};

// Test: Constructor validation
TEST_F(BloomFilterTest, ConstructorValidation) {
    // Valid construction
    EXPECT_NO_THROW(BloomFilter(1024, 5));
    EXPECT_NO_THROW(BloomFilter(1, 1));
    
    // Invalid size
    EXPECT_THROW(BloomFilter(0, 5), std::invalid_argument);
    
    // Invalid number of hash functions
    EXPECT_THROW(BloomFilter(1024, 0), std::invalid_argument);
}

// Test: Basic add and contains operations
TEST_F(BloomFilterTest, BasicAddAndContains) {
    BloomFilter filter(1024, 5);
    
    // Empty filter should not contain any elements
    EXPECT_FALSE(filter.contains("tx_hash_1"));
    EXPECT_FALSE(filter.contains("tx_hash_2"));
    
    // Add element and verify it's found
    filter.add("tx_hash_1");
    EXPECT_TRUE(filter.contains("tx_hash_1"));
    EXPECT_FALSE(filter.contains("tx_hash_2"));
    
    // Add more elements
    filter.add("tx_hash_2");
    filter.add("tx_hash_3");
    
    EXPECT_TRUE(filter.contains("tx_hash_1"));
    EXPECT_TRUE(filter.contains("tx_hash_2"));
    EXPECT_TRUE(filter.contains("tx_hash_3"));
    EXPECT_FALSE(filter.contains("tx_hash_4"));
}

// Test: Clear operation
TEST_F(BloomFilterTest, ClearOperation) {
    BloomFilter filter(1024, 5);
    
    // Add elements
    filter.add("tx_hash_1");
    filter.add("tx_hash_2");
    filter.add("tx_hash_3");
    
    EXPECT_TRUE(filter.contains("tx_hash_1"));
    EXPECT_TRUE(filter.contains("tx_hash_2"));
    EXPECT_TRUE(filter.contains("tx_hash_3"));
    
    // Clear the filter
    filter.clear();
    
    // All elements should be gone
    EXPECT_FALSE(filter.contains("tx_hash_1"));
    EXPECT_FALSE(filter.contains("tx_hash_2"));
    EXPECT_FALSE(filter.contains("tx_hash_3"));
    
    // Element count should reset
    EXPECT_EQ(filter.approximate_count(), 0u);
}

// Test: Duplicate elements
TEST_F(BloomFilterTest, DuplicateElements) {
    BloomFilter filter(1024, 5);
    
    // Add same element multiple times
    filter.add("tx_hash_1");
    filter.add("tx_hash_1");
    filter.add("tx_hash_1");
    
    // Should still be found
    EXPECT_TRUE(filter.contains("tx_hash_1"));
    
    // Count tracks add calls (not unique elements)
    EXPECT_EQ(filter.approximate_count(), 3u);
}

// Test: False positive rate validation
TEST_F(BloomFilterTest, FalsePositiveRateValidation) {
    // Create a smaller filter for faster testing
    size_t size_bits = 10000;
    size_t num_hashes = 5;
    size_t num_elements = 1000;
    
    BloomFilter filter(size_bits, num_hashes);
    
    // Add known elements
    std::set<std::string> known_elements;
    for (size_t i = 0; i < num_elements; ++i) {
        std::string element = "tx_hash_" + std::to_string(i);
        filter.add(element);
        known_elements.insert(element);
    }
    
    // Test with elements not in the filter
    size_t false_positives = 0;
    size_t test_count = 10000;
    
    for (size_t i = num_elements; i < num_elements + test_count; ++i) {
        std::string element = "tx_hash_" + std::to_string(i);
        if (filter.contains(element)) {
            false_positives++;
        }
    }
    
    double observed_fp_rate = static_cast<double>(false_positives) / test_count;
    double theoretical_fp_rate = filter.false_positive_rate();
    
    // Observed rate should be within reasonable bounds of theoretical rate
    // Allow 50% deviation due to statistical variance
    EXPECT_LT(observed_fp_rate, theoretical_fp_rate * 1.5);
    
    // Observed rate should not be suspiciously low (would indicate bug)
    EXPECT_GT(observed_fp_rate, theoretical_fp_rate * 0.5);
    
    std::cout << "Theoretical FP rate: " << theoretical_fp_rate * 100 << "%\n";
    std::cout << "Observed FP rate: " << observed_fp_rate * 100 << "%\n";
}

// Test: No false negatives
TEST_F(BloomFilterTest, NoFalseNegatives) {
    BloomFilter filter(10000, 5);
    
    // Add many elements
    std::vector<std::string> elements;
    for (size_t i = 0; i < 1000; ++i) {
        std::string element = "tx_hash_" + std::to_string(i);
        elements.push_back(element);
        filter.add(element);
    }
    
    // Verify all added elements are found (no false negatives allowed)
    for (const auto& element : elements) {
        EXPECT_TRUE(filter.contains(element)) 
            << "False negative detected for element: " << element;
    }
}

// Test: Thread-safe concurrent add operations
TEST_F(BloomFilterTest, ConcurrentAdd) {
    BloomFilter filter(100000, 5);
    
    constexpr size_t num_threads = 10;
    constexpr size_t elements_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    // Launch multiple threads adding elements concurrently
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&filter, t]() {
            for (size_t i = 0; i < elements_per_thread; ++i) {
                std::string element = "thread_" + std::to_string(t) + 
                                     "_tx_" + std::to_string(i);
                filter.add(element);
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all elements were added correctly
    for (size_t t = 0; t < num_threads; ++t) {
        for (size_t i = 0; i < elements_per_thread; ++i) {
            std::string element = "thread_" + std::to_string(t) + 
                                 "_tx_" + std::to_string(i);
            EXPECT_TRUE(filter.contains(element))
                << "Element not found after concurrent add: " << element;
        }
    }
    
    // Check approximate count
    EXPECT_EQ(filter.approximate_count(), num_threads * elements_per_thread);
}

// Test: Thread-safe concurrent read operations
TEST_F(BloomFilterTest, ConcurrentReadWrite) {
    BloomFilter filter(100000, 5);
    
    // Pre-populate filter
    for (size_t i = 0; i < 500; ++i) {
        filter.add("tx_hash_" + std::to_string(i));
    }
    
    constexpr size_t num_reader_threads = 5;
    constexpr size_t num_writer_threads = 5;
    constexpr size_t operations_per_thread = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<size_t> read_errors{0};
    
    // Launch reader threads
    for (size_t t = 0; t < num_reader_threads; ++t) {
        threads.emplace_back([&filter, &read_errors]() {
            for (size_t i = 0; i < operations_per_thread; ++i) {
                // Read existing elements
                std::string element = "tx_hash_" + std::to_string(i % 500);
                if (!filter.contains(element)) {
                    read_errors.fetch_add(1);
                }
            }
        });
    }
    
    // Launch writer threads
    for (size_t t = 0; t < num_writer_threads; ++t) {
        threads.emplace_back([&filter, t]() {
            for (size_t i = 0; i < operations_per_thread; ++i) {
                std::string element = "new_tx_" + std::to_string(t) + 
                                     "_" + std::to_string(i);
                filter.add(element);
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // No false negatives should occur during concurrent access
    EXPECT_EQ(read_errors.load(), 0u) 
        << "False negatives detected during concurrent read/write";
}

// Test: Empty string handling
TEST_F(BloomFilterTest, EmptyStringHandling) {
    BloomFilter filter(1024, 5);
    
    // Add empty string
    filter.add("");
    
    // Should be found
    EXPECT_TRUE(filter.contains(""));
    
    // Other strings should not be found
    EXPECT_FALSE(filter.contains("non_empty"));
}

// Test: Large transaction hash strings (realistic blockchain hashes)
TEST_F(BloomFilterTest, RealisticTransactionHashes) {
    BloomFilter filter(100000, 5);
    
    // Simulate realistic Ethereum transaction hashes (64 hex characters)
    std::vector<std::string> tx_hashes = {
        "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
        "0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321",
        "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    };
    
    // Add hashes
    for (const auto& hash : tx_hashes) {
        filter.add(hash);
    }
    
    // Verify all are found
    for (const auto& hash : tx_hashes) {
        EXPECT_TRUE(filter.contains(hash)) 
            << "Transaction hash not found: " << hash;
    }
    
    // Verify similar but different hashes are not found
    EXPECT_FALSE(filter.contains("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdee"));
}

// Test: Configuration parameters
TEST_F(BloomFilterTest, ConfigurationParameters) {
    size_t size_bits = 8192;
    size_t num_hashes = 7;
    
    BloomFilter filter(size_bits, num_hashes);
    
    EXPECT_EQ(filter.size_bits(), size_bits);
    EXPECT_EQ(filter.num_hash_functions(), num_hashes);
    EXPECT_EQ(filter.approximate_count(), 0u);
    
    filter.add("element");
    EXPECT_EQ(filter.approximate_count(), 1u);
}

// Test: False positive rate calculation
TEST_F(BloomFilterTest, FalsePositiveRateCalculation) {
    BloomFilter filter(1000, 3);
    
    // Empty filter has 0% false positive rate
    EXPECT_DOUBLE_EQ(filter.false_positive_rate(), 0.0);
    
    // Add elements and check rate increases
    double prev_rate = 0.0;
    for (size_t i = 0; i < 100; ++i) {
        filter.add("element_" + std::to_string(i));
        double current_rate = filter.false_positive_rate();
        EXPECT_GE(current_rate, prev_rate);
        prev_rate = current_rate;
    }
    
    // Rate should be between 0 and 1
    EXPECT_GT(filter.false_positive_rate(), 0.0);
    EXPECT_LT(filter.false_positive_rate(), 1.0);
}

// Test: Replay attack scenario
TEST_F(BloomFilterTest, ReplayAttackPrevention) {
    BloomFilter filter(100000, 5);
    
    // Simulate processing transactions
    std::string tx1 = "0xabcdef1234567890";
    std::string tx2 = "0x1234567890abcdef";
    std::string tx3 = "0xfedcba0987654321";
    
    // First time processing - should not be present
    EXPECT_FALSE(filter.contains(tx1));
    filter.add(tx1);
    EXPECT_TRUE(filter.contains(tx1));  // Now marked as processed
    
    // Replay attempt - should be detected
    EXPECT_TRUE(filter.contains(tx1));  // Replay detected!
    
    // Other transactions should still work
    EXPECT_FALSE(filter.contains(tx2));
    filter.add(tx2);
    EXPECT_TRUE(filter.contains(tx2));
    
    EXPECT_FALSE(filter.contains(tx3));
    filter.add(tx3);
    EXPECT_TRUE(filter.contains(tx3));
    
    // All previous transactions marked as processed
    EXPECT_TRUE(filter.contains(tx1));
    EXPECT_TRUE(filter.contains(tx2));
    EXPECT_TRUE(filter.contains(tx3));
}
