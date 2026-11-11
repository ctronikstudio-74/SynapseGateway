#pragma once

#include "gateway/blockchain/blockchain_query.h"
#include "gateway/core/Configuration.h"
#include "gateway/core/cache.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace gateway::core {

struct TrustWeights {
    double success_rate_weight = 0.5;
    double response_time_weight = 0.2;
    double feedback_weight = 0.3;
};

struct ReputationCheckResult {
    enum class Status {
        APPROVED,
        BELOW_THRESHOLD,
        NO_REPUTATION_DATA,
        PROOF_VALIDATION_FAILED,
        VERIFICATION_ERROR
    };

    Status status = Status::VERIFICATION_ERROR;
    std::string message;
    double composite_trust_score = 0.0;
    std::optional<gateway::blockchain::ReputationData> reputation_data;
};

[[nodiscard]] double responseTimeScore(double avg_response_time_ms);
[[nodiscard]] double calculateTrustScore(const gateway::blockchain::ReputationData& data, TrustWeights weights);

class ReputationChecker {
public:
    static constexpr std::chrono::seconds kCacheTtl{300};
    static constexpr std::chrono::milliseconds kProofTimeout{200};

    ReputationChecker(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                      std::shared_ptr<Configuration> config,
                      TrustWeights weights = {},
                      std::size_t cache_entries = 1024);

    [[nodiscard]] ReputationCheckResult checkReputation(const std::string& wallet_address,
                                                        Network network,
                                                        const std::string& endpoint_path,
                                                        const std::string& proof_hex = {},
                                                        std::uint64_t now_unix = 0);

    void updateConfiguration(std::shared_ptr<Configuration> config);

private:
    [[nodiscard]] std::shared_ptr<Configuration> configuration() const;

    std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain_;
    std::shared_ptr<Configuration> config_;
    mutable std::mutex config_mutex_;
    TrustWeights weights_;
    Cache<std::string, gateway::blockchain::ReputationData> cache_;
};

}  // namespace gateway::core
