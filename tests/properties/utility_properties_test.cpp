#include "gateway/core/cache.h"
#include "gateway/utils/bloom_filter.h"
#include "gateway/utils/rate_limiter.h"

#include "../support/property.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct ManualClock {
    using rep = std::int64_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<ManualClock>;
    static constexpr bool is_steady = false;
    static time_point now() noexcept { return current; }
    static time_point current;
};

ManualClock::time_point ManualClock::current{};

}  // namespace

TEST(UtilityProperties, ReplayMembershipAndFalsePositiveBound) {
    gateway::test::checkProperty("bloom replay", [](std::mt19937& rng) {
        gateway::utils::BloomFilter filter(16384, 4);
        std::uniform_int_distribution<int> suffix(0, 1000000);
        std::vector<std::string> inserted;
        inserted.reserve(80);
        for (int i = 0; i < 80; ++i) {
            const std::string hash = "tx-" + std::to_string(i) + "-" + std::to_string(suffix(rng));
            EXPECT_FALSE(filter.contains(hash));
            filter.add(hash);
            EXPECT_TRUE(filter.contains(hash));
            filter.add(hash);
            EXPECT_TRUE(filter.contains(hash));
            inserted.push_back(hash);
        }
        for (const std::string& hash : inserted) {
            EXPECT_TRUE(filter.contains(hash));
        }

        int false_positives = 0;
        constexpr int kTrials = 200;
        for (int i = 0; i < kTrials; ++i) {
            const std::string unseen = "unseen-" + std::to_string(suffix(rng)) + "-" + std::to_string(i);
            if (filter.contains(unseen)) {
                ++false_positives;
            }
        }
        const double empirical = static_cast<double>(false_positives) / static_cast<double>(kTrials);
        const double bound = std::max(0.05, filter.false_positive_rate() * 5.0 + 0.02);
        EXPECT_LT(empirical, bound);
    });
}

TEST(UtilityProperties, CacheTtlAndLru) {
    gateway::test::checkProperty("cache ttl", [](std::mt19937& rng) {
        ManualClock::current = ManualClock::time_point{};
        std::uniform_int_distribution<int> ttl_seconds(1, 30);
        const auto ttl = std::chrono::seconds{ttl_seconds(rng)};
        gateway::core::Cache<int, int, ManualClock> cache(8, ttl);
        std::uniform_int_distribution<int> key_dist(1, 6);
        const int key = key_dist(rng);
        const int value = key_dist(rng) + 10;
        cache.put(key, value, ttl);
        ManualClock::current += ttl - std::chrono::seconds{1};
        EXPECT_EQ(cache.get(key), value);
        ManualClock::current += std::chrono::seconds{2};
        EXPECT_FALSE(cache.get(key).has_value());
    });

    ManualClock::current = ManualClock::time_point{};
    gateway::core::Cache<int, std::string, ManualClock> reputation(4, std::chrono::seconds{300});
    reputation.put(7, "score");
    ManualClock::current += std::chrono::seconds{299};
    EXPECT_EQ(reputation.get(7), "score");
    ManualClock::current += std::chrono::seconds{2};
    EXPECT_FALSE(reputation.get(7).has_value());

    ManualClock::current = ManualClock::time_point{};
    gateway::core::Cache<int, int, ManualClock> lru(2, std::chrono::seconds{100});
    lru.put(1, 10);
    lru.put(2, 20);
    EXPECT_EQ(lru.get(1), 10);
    lru.put(3, 30);
    EXPECT_FALSE(lru.get(2).has_value());
    EXPECT_EQ(lru.get(1), 10);
    EXPECT_EQ(lru.get(3), 30);
}

TEST(UtilityProperties, RateLimitSlidingWindow) {
    gateway::test::checkProperty("rate limit", [](std::mt19937& rng) {
        std::uniform_int_distribution<int> limit_dist(1, 8);
        const auto limit = static_cast<std::uint32_t>(limit_dist(rng));
        gateway::utils::RateLimiter limiter(limit);
        const std::string wallet = "0xabc";
        const auto origin = gateway::utils::RateLimiter::time_point{};
        std::uniform_int_distribution<int> gap(0, 20);
        std::vector<int> accepted;
        int cursor = 0;
        for (int request = 0; request < 40; ++request) {
            cursor += gap(rng);
            const auto now = origin + std::chrono::seconds{cursor};
            int in_window = 0;
            for (const int stamp : accepted) {
                if (stamp > cursor - 60 && stamp <= cursor) {
                    ++in_window;
                }
            }
            const bool allowed = in_window < static_cast<int>(limit);
            EXPECT_EQ(limiter.checkLimit(wallet, now), allowed);
            if (allowed) {
                limiter.recordRequest(wallet, now);
                accepted.push_back(cursor);
            }
        }
    });
}
