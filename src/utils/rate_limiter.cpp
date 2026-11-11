#include "gateway/utils/rate_limiter.h"

#include <algorithm>

namespace gateway::utils {

RateLimiter::RateLimiter(uint32_t default_limit_per_minute) : default_limit_(default_limit_per_minute) {}

bool RateLimiter::checkLimit(const std::string& wallet_address, time_point now) {
    std::lock_guard lock(mutex_);
    auto& timestamps = windows_[wallet_address];
    prune(timestamps, now);
    return timestamps.size() < static_cast<size_t>(limitForUnlocked(wallet_address));
}

bool RateLimiter::checkLimit(const std::string& wallet_address, uint32_t limit_per_minute, time_point now) {
    std::lock_guard lock(mutex_);
    auto& timestamps = windows_[wallet_address];
    prune(timestamps, now);
    return timestamps.size() < static_cast<size_t>(limit_per_minute);
}

std::chrono::seconds RateLimiter::retryAfter(const std::string& wallet_address, time_point now) const {
    std::lock_guard lock(mutex_);
    const auto it = windows_.find(wallet_address);
    if (it == windows_.end() || it->second.empty()) {
        return std::chrono::seconds{1};
    }
    const auto available = it->second.front() + kWindow;
    if (available <= now) {
        return std::chrono::seconds{1};
    }
    auto wait = std::chrono::duration_cast<std::chrono::seconds>(available - now) + std::chrono::seconds{1};
    if (wait.count() < 1) {
        wait = std::chrono::seconds{1};
    }
    return wait;
}

void RateLimiter::recordRequest(const std::string& wallet_address, time_point now) {
    std::lock_guard lock(mutex_);
    auto& timestamps = windows_[wallet_address];
    prune(timestamps, now);
    timestamps.push_back(now);
}

void RateLimiter::setLimit(const std::string& wallet_address, uint32_t limit_per_minute) {
    std::lock_guard lock(mutex_);
    limits_[wallet_address] = limit_per_minute;
}

uint32_t RateLimiter::limitFor(const std::string& wallet_address) const {
    std::lock_guard lock(mutex_);
    return limitForUnlocked(wallet_address);
}

void RateLimiter::prune(std::deque<time_point>& timestamps, time_point now) const {
    const auto cutoff = now - kWindow;
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
                                    [cutoff](time_point stamp) { return stamp <= cutoff; }),
                     timestamps.end());
}

uint32_t RateLimiter::limitForUnlocked(const std::string& wallet_address) const {
    const auto it = limits_.find(wallet_address);
    if (it == limits_.end()) {
        return default_limit_;
    }
    return it->second;
}

}  // namespace gateway::utils
