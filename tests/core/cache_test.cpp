#include "gateway/core/cache.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct ManualClock {
    using time_point = std::chrono::steady_clock::time_point;
    static inline time_point current{};
    static time_point now() { return current; }
};

using TestCache = gateway::core::Cache<std::string, int, ManualClock>;

}  // namespace

TEST(CacheTest, RejectsInvalidConstruction) {
    EXPECT_THROW((gateway::core::Cache<std::string, int>(0, 10s)), std::invalid_argument);
    EXPECT_THROW((gateway::core::Cache<std::string, int>(4, std::chrono::seconds{-1})),
                 std::invalid_argument);
}

TEST(CacheTest, GetPutAndMiss) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(4, 30s);

    EXPECT_FALSE(cache.get("missing").has_value());
    cache.put("alpha", 7);
    ASSERT_TRUE(cache.get("alpha").has_value());
    EXPECT_EQ(*cache.get("alpha"), 7);

    const auto stats = cache.getStats();
    EXPECT_EQ(stats.hits, 2u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.current_size, 1u);
    EXPECT_DOUBLE_EQ(stats.hit_rate, 2.0 / 3.0);
}

TEST(CacheTest, ExpiresAfterTtl) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(4, 10s);

    cache.put("alpha", 1);
    ManualClock::current += 9s;
    EXPECT_EQ(cache.get("alpha"), std::optional<int>{1});

    ManualClock::current += 1s;
    EXPECT_FALSE(cache.get("alpha").has_value());
    EXPECT_EQ(cache.getStats().current_size, 0u);
}

TEST(CacheTest, CustomTtlOverridesDefault) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(4, 60s);

    cache.put("short", 1, 5s);
    ManualClock::current += 5s;
    EXPECT_FALSE(cache.get("short").has_value());
}

TEST(CacheTest, EvictsLeastRecentlyUsed) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(2, 60s);

    cache.put("a", 1);
    cache.put("b", 2);
    EXPECT_EQ(cache.get("a"), std::optional<int>{1});
    cache.put("c", 3);

    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_EQ(cache.get("a"), std::optional<int>{1});
    EXPECT_EQ(cache.get("c"), std::optional<int>{3});
    EXPECT_EQ(cache.getStats().evictions, 1u);
    EXPECT_EQ(cache.getStats().current_size, 2u);
}

TEST(CacheTest, UpdatingExistingKeyDoesNotGrowOrEvict) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(2, 60s);

    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("a", 9);
    cache.put("c", 3);

    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_EQ(cache.get("a"), std::optional<int>{9});
    EXPECT_EQ(cache.get("c"), std::optional<int>{3});
    EXPECT_EQ(cache.getStats().current_size, 2u);
}

TEST(CacheTest, RemoveAndClear) {
    ManualClock::current = ManualClock::time_point{};
    TestCache cache(4, 60s);

    cache.put("a", 1);
    cache.put("b", 2);
    cache.remove("a");
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_EQ(cache.get("b"), std::optional<int>{2});

    cache.clear();
    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_EQ(cache.getStats().current_size, 0u);
}

TEST(CacheTest, ConcurrentPutAndGet) {
    gateway::core::Cache<int, int> cache(1000, 30s);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;

    std::vector<std::thread> threads;
    for (int thread_id = 0; thread_id < kThreads; ++thread_id) {
        threads.emplace_back([&cache, thread_id]() {
            for (int i = 0; i < kPerThread; ++i) {
                const int key = thread_id * kPerThread + i;
                cache.put(key, key * 2);
                const auto value = cache.get(key);
                EXPECT_TRUE(value.has_value());
                if (value) {
                    EXPECT_EQ(*value, key * 2);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(cache.getStats().current_size, static_cast<size_t>(kThreads * kPerThread));
}
