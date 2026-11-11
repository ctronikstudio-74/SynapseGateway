#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gateway::utils {

/**
 * @brief Per-wallet sliding-window rate limiter.
 *
 * A request is rejected when the number of recorded timestamps inside the
 * trailing 60-second window is already at the configured limit. checkLimit()
 * does not record the request; callers record only the requests they accept.
 *
 * Requirements: 11.4, 11.5
 */
class RateLimiter {
public:
    using time_point = std::chrono::steady_clock::time_point;

    explicit RateLimiter(uint32_t default_limit_per_minute);

    /**
     * @brief Return true when the wallet is still under its per-minute limit.
     */
    bool checkLimit(const std::string& wallet_address, time_point now = std::chrono::steady_clock::now());

    /**
     * @brief Same sliding window, with the limit supplied by the matched endpoint.
     */
    bool checkLimit(const std::string& wallet_address, uint32_t limit_per_minute, time_point now);

    /**
     * @brief Seconds until the oldest request in the window ages out. Always at least 1.
     */
    [[nodiscard]] std::chrono::seconds retryAfter(const std::string& wallet_address,
                                                  time_point now = std::chrono::steady_clock::now()) const;

    /**
     * @brief Record an accepted request timestamp for the wallet.
     */
    void recordRequest(const std::string& wallet_address,
                       time_point now = std::chrono::steady_clock::now());

    /**
     * @brief Override the per-minute limit for one wallet address.
     */
    void setLimit(const std::string& wallet_address, uint32_t limit_per_minute);

    [[nodiscard]] uint32_t limitFor(const std::string& wallet_address) const;

private:
    static constexpr std::chrono::seconds kWindow{60};

    void prune(std::deque<time_point>& timestamps, time_point now) const;
    [[nodiscard]] uint32_t limitForUnlocked(const std::string& wallet_address) const;

    uint32_t default_limit_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::deque<time_point>> windows_;
    std::unordered_map<std::string, uint32_t> limits_;
};

}  // namespace gateway::utils
