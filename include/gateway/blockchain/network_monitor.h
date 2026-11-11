#pragma once

#include "gateway/blockchain/BlockchainConnector.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace gateway::blockchain {

class NetworkMonitor {
public:
    NetworkMonitor(std::shared_ptr<BlockchainConnector> connector,
                   std::chrono::milliseconds health_interval,
                   std::chrono::milliseconds gas_interval);
    ~NetworkMonitor();

    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

private:
    void run();

    std::shared_ptr<BlockchainConnector> connector_;
    std::chrono::milliseconds health_interval_;
    std::chrono::milliseconds gas_interval_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace gateway::blockchain
