#include "gateway/utils/rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using gateway::utils::RateLimiter;
using namespace std::chrono_literals;

namespace {

RateLimiter::time_point at(int seconds) {
    return RateLimiter::time_point{} + std::chrono::seconds{seconds};
}

}  // namespace

TEST(RateLimiterTest, AllowsRequestsUnderTheLimit) {
    RateLimiter limiter(2);

    EXPECT_TRUE(limiter.checkLimit("0xabc", at(0)));
    limiter.recordRequest("0xabc", at(0));
    EXPECT_TRUE(limiter.checkLimit("0xabc", at(1)));
    limiter.recordRequest("0xabc", at(1));
    EXPECT_FALSE(limiter.checkLimit("0xabc", at(2)));
}

TEST(RateLimiterTest, SlidingWindowDropsExpiredTimestamps) {
    RateLimiter limiter(1);

    limiter.recordRequest("0xabc", at(0));
    EXPECT_FALSE(limiter.checkLimit("0xabc", at(59)));
    EXPECT_TRUE(limiter.checkLimit("0xabc", at(60)));
    limiter.recordRequest("0xabc", at(60));
    EXPECT_FALSE(limiter.checkLimit("0xabc", at(61)));
}

TEST(RateLimiterTest, LimitsAreIndependentPerWallet) {
    RateLimiter limiter(1);

    limiter.recordRequest("0xaaa", at(0));
    EXPECT_FALSE(limiter.checkLimit("0xaaa", at(1)));
    EXPECT_TRUE(limiter.checkLimit("0xbbb", at(1)));
}

TEST(RateLimiterTest, PerWalletLimitOverride) {
    RateLimiter limiter(10);
    limiter.setLimit("0xvip", 1);

    EXPECT_EQ(limiter.limitFor("0xvip"), 1u);
    EXPECT_EQ(limiter.limitFor("0xother"), 10u);

    limiter.recordRequest("0xvip", at(0));
    EXPECT_FALSE(limiter.checkLimit("0xvip", at(1)));
    EXPECT_TRUE(limiter.checkLimit("0xother", at(1)));
}

TEST(RateLimiterTest, ZeroLimitRejectsEveryRequest) {
    RateLimiter limiter(0);
    EXPECT_FALSE(limiter.checkLimit("0xabc", at(5)));
}

TEST(RateLimiterTest, ConcurrentChecksDoNotCorruptTheWindow) {
    constexpr int kPerThread = 40;
    RateLimiter limiter(static_cast<uint32_t>(kPerThread));
    constexpr int kThreads = 4;

    std::vector<std::thread> threads;
    for (int thread_id = 0; thread_id < kThreads; ++thread_id) {
        threads.emplace_back([&limiter, thread_id]() {
            const std::string wallet = "wallet-" + std::to_string(thread_id);
            for (int i = 0; i < kPerThread; ++i) {
                if (limiter.checkLimit(wallet, at(0))) {
                    limiter.recordRequest(wallet, at(0));
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    for (int thread_id = 0; thread_id < kThreads; ++thread_id) {
        const std::string wallet = "wallet-" + std::to_string(thread_id);
        EXPECT_FALSE(limiter.checkLimit(wallet, at(0)));
    }
}
