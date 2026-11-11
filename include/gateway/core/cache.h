#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace gateway::core {

template <typename Key>
concept CacheKey = std::copyable<Key> && requires(const Key& key) {
    { std::hash<Key>{}(key) } -> std::convertible_to<std::size_t>;
};

/**
 * @brief Thread-safe LRU cache with per-entry TTL.
 *
 * Most-recently used keys sit at the front of the LRU list. get() counts as a
 * use and therefore takes an exclusive lock. Clock is a template parameter so
 * tests can advance time without sleeping.
 *
 * Requirements: 2.8, 5.8, 10.2
 */
template <CacheKey Key, std::copyable Value, typename Clock = std::chrono::steady_clock>
class Cache {
public:
    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
        size_t evictions = 0;
        size_t current_size = 0;
        double hit_rate = 0.0;
    };

    explicit Cache(size_t max_size, std::chrono::seconds default_ttl)
        : max_size_(max_size), default_ttl_(default_ttl) {
        if (max_size_ == 0) {
            throw std::invalid_argument("Cache max_size must be greater than 0");
        }
        if (default_ttl_.count() < 0) {
            throw std::invalid_argument("Cache default TTL must be non-negative");
        }
    }

    [[nodiscard]] std::optional<Value> get(const Key& key) {
        std::unique_lock lock(mutex_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            misses_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        if (isExpired(it->second)) {
            eraseMapped(it);
            misses_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_iter);
        hits_.fetch_add(1, std::memory_order_relaxed);
        return it->second.value;
    }

    void put(const Key& key, const Value& value,
             std::optional<std::chrono::seconds> ttl = std::nullopt) {
        const auto effective_ttl = ttl.value_or(default_ttl_);
        if (effective_ttl.count() < 0) {
            throw std::invalid_argument("Cache TTL must be non-negative");
        }

        std::unique_lock lock(mutex_);
        const auto expiry = Clock::now() + effective_ttl;
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            it->second.value = value;
            it->second.expiry = expiry;
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_iter);
            return;
        }

        if (cache_map_.size() >= max_size_) {
            evictLRU();
        }

        lru_list_.push_front(key);
        cache_map_.emplace(key, CacheEntry{value, expiry, lru_list_.begin()});
    }

    void remove(const Key& key) {
        std::unique_lock lock(mutex_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return;
        }
        eraseMapped(it);
    }

    void clear() {
        std::unique_lock lock(mutex_);
        cache_map_.clear();
        lru_list_.clear();
    }

    [[nodiscard]] Stats getStats() const {
        std::shared_lock lock(mutex_);
        Stats stats;
        stats.hits = hits_.load(std::memory_order_relaxed);
        stats.misses = misses_.load(std::memory_order_relaxed);
        stats.evictions = evictions_.load(std::memory_order_relaxed);
        stats.current_size = cache_map_.size();
        const auto total = stats.hits + stats.misses;
        stats.hit_rate =
            total == 0 ? 0.0 : static_cast<double>(stats.hits) / static_cast<double>(total);
        return stats;
    }

    [[nodiscard]] size_t max_size() const noexcept { return max_size_; }

private:
    using ListIterator = typename std::list<Key>::iterator;

    struct CacheEntry {
        Value value;
        typename Clock::time_point expiry;
        ListIterator lru_iter;
    };

    [[nodiscard]] bool isExpired(const CacheEntry& entry) const {
        return Clock::now() >= entry.expiry;
    }

    void eraseMapped(typename std::unordered_map<Key, CacheEntry>::iterator it) {
        lru_list_.erase(it->second.lru_iter);
        cache_map_.erase(it);
    }

    void evictLRU() {
        if (lru_list_.empty()) {
            return;
        }
        Key oldest = lru_list_.back();
        cache_map_.erase(oldest);
        lru_list_.pop_back();
        evictions_.fetch_add(1, std::memory_order_relaxed);
    }

    size_t max_size_;
    std::chrono::seconds default_ttl_;
    std::list<Key> lru_list_;
    std::unordered_map<Key, CacheEntry> cache_map_;
    mutable std::shared_mutex mutex_;
    std::atomic<size_t> hits_{0};
    std::atomic<size_t> misses_{0};
    std::atomic<size_t> evictions_{0};
};

}  // namespace gateway::core
