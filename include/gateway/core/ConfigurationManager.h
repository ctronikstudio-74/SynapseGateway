#pragma once

#include "gateway/core/Configuration.h"
#include "gateway/core/Expected.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gateway::core {

class ConfigurationWatcher {
public:
    ConfigurationWatcher(std::filesystem::path path,
                         std::function<void(Configuration)> on_reload,
                         std::chrono::milliseconds poll_interval);
    ~ConfigurationWatcher();

    ConfigurationWatcher(const ConfigurationWatcher&) = delete;
    ConfigurationWatcher& operator=(const ConfigurationWatcher&) = delete;

private:
    void run();

    std::filesystem::path path_;
    std::function<void(Configuration)> on_reload_;
    std::chrono::milliseconds poll_interval_;
    std::atomic<bool> stop_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    std::thread thread_;
};

/**
 * @brief Loads, validates, serializes, and watches AgentGateway TOML configuration.
 *
 * Requirements: 8.6, 8.8, 14.1, 14.2, 14.6, 14.7
 */
class ConfigurationManager {
public:
    [[nodiscard]] static Expected<Configuration> loadFromFile(const std::filesystem::path& config_path);

    /**
     * @brief Replace ${NAME} placeholders with environment variables.
     *
     * Unset names are left unchanged so validation can report the field.
     */
    [[nodiscard]] static std::string expandEnvironmentPlaceholders(std::string_view text);
    [[nodiscard]] static Expected<Configuration> loadFromString(std::string_view toml_text);
    [[nodiscard]] static std::vector<std::string> validate(const Configuration& config);
    [[nodiscard]] static std::string serialize(const Configuration& config);
    [[nodiscard]] static std::unique_ptr<ConfigurationWatcher> watchForChanges(
        const std::filesystem::path& config_path,
        std::function<void(Configuration)> on_reload,
        std::chrono::milliseconds poll_interval = std::chrono::seconds(1));
};

}  // namespace gateway::core
