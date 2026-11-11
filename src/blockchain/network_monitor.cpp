#include "gateway/blockchain/network_monitor.h"

#include <algorithm>

namespace gateway::blockchain {

NetworkMonitor::NetworkMonitor(std::shared_ptr<BlockchainConnector> connector,
                               std::chrono::milliseconds health_interval,
                               std::chrono::milliseconds gas_interval)
    : connector_(std::move(connector))
    , health_interval_(health_interval.count() <= 0 ? std::chrono::milliseconds{10000} : health_interval)
    , gas_interval_(gas_interval.count() <= 0 ? std::chrono::milliseconds{60000} : gas_interval)
    , thread_([this] { run(); }) {}

NetworkMonitor::~NetworkMonitor() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void NetworkMonitor::run() {
    auto next_health = std::chrono::steady_clock::now();
    auto next_gas = next_health;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_health) {
            connector_->refreshHealth();
            next_health = now + health_interval_;
        }
        if (now >= next_gas) {
            connector_->refreshGasFees();
            next_gas = now + gas_interval_;
        }
        const auto wake = std::min(next_health, next_gas);
        std::unique_lock lock(mutex_);
        if (cv_.wait_until(lock, wake, [this] { return stop_; })) {
            return;
        }
    }
}

}  // namespace gateway::blockchain
