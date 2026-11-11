#include "gateway/core/ReputationChecker.h"

#include "gateway/core/PaymentProcessor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace gateway::core {
namespace {

std::string cacheKey(Network network, const std::string& wallet) {
    std::string lower = wallet;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return NetworkToString(network) + ":" + lower;
}

}  // namespace

double responseTimeScore(double avg_response_time_ms) {
    if (!std::isfinite(avg_response_time_ms) || avg_response_time_ms < 0.0) {
        avg_response_time_ms = 0.0;
    }
    return 1.0 / (1.0 + avg_response_time_ms / 1000.0);
}

double calculateTrustScore(const gateway::blockchain::ReputationData& data, TrustWeights weights) {
    const double components[] = {std::clamp(data.success_rate, 0.0, 1.0),
                                 std::clamp(responseTimeScore(data.avg_response_time_ms), 0.0, 1.0),
                                 std::clamp(data.feedback_score, 0.0, 1.0)};
    const double applied[] = {weights.success_rate_weight, weights.response_time_weight, weights.feedback_weight};
    const double weight_sum = applied[0] + applied[1] + applied[2];
    if (!(weight_sum > 0.0)) {
        return 0.0;
    }
    const double raw = (applied[0] * components[0] + applied[1] * components[1] + applied[2] * components[2]) / weight_sum;
    return std::clamp(raw, 0.0, 1.0);
}

ReputationChecker::ReputationChecker(std::shared_ptr<gateway::blockchain::BlockchainQuery> blockchain,
                                     std::shared_ptr<Configuration> config,
                                     TrustWeights weights,
                                     std::size_t cache_entries)
    : blockchain_(std::move(blockchain))
    , config_(std::move(config))
    , weights_(weights)
    , cache_(cache_entries, kCacheTtl) {}

ReputationCheckResult ReputationChecker::checkReputation(const std::string& wallet_address,
                                                         Network network,
                                                         const std::string& endpoint_path,
                                                         const std::string& proof_hex,
                                                         std::uint64_t now_unix) {
    ReputationCheckResult result;
    if (!blockchain_->supportsNetwork(network)) {
        result.status = ReputationCheckResult::Status::VERIFICATION_ERROR;
        result.message = "reputation registry is not configured for this network";
        return result;
    }
    const std::shared_ptr<Configuration> config = configuration();
    const EndpointConfig& endpoint = matchEndpoint(*config, endpoint_path);
    const std::string key = cacheKey(network, wallet_address);

    std::optional<gateway::blockchain::ReputationData> reputation = cache_.get(key);
    if (!reputation) {
        const auto lookup = blockchain_->getReputationData(wallet_address, network);
        if (lookup.status == gateway::blockchain::LookupStatus::Error ||
            lookup.status == gateway::blockchain::LookupStatus::Unavailable) {
            result.status = ReputationCheckResult::Status::VERIFICATION_ERROR;
            result.message = "reputation registry query failed";
            return result;
        }
        if (lookup.status == gateway::blockchain::LookupStatus::Found && lookup.reputation) {
            reputation = lookup.reputation;
            cache_.put(key, *reputation, kCacheTtl);
        }
    }

    if (!reputation) {
        if (endpoint.new_agents_allowed) {
            result.status = ReputationCheckResult::Status::APPROVED;
            result.message = "new agent policy allow";
            result.composite_trust_score = 0.0;
        } else {
            result.status = ReputationCheckResult::Status::NO_REPUTATION_DATA;
            result.message = "no reputation data and new agents are rejected";
            return result;
        }
    } else {
        result.reputation_data = reputation;
        result.composite_trust_score = calculateTrustScore(*reputation, weights_);
        if (result.composite_trust_score < endpoint.trust_threshold) {
            result.status = ReputationCheckResult::Status::BELOW_THRESHOLD;
            result.message = "trust score " + std::to_string(result.composite_trust_score) + " below threshold " +
                             std::to_string(endpoint.trust_threshold) +
                             " (success_rate=" + std::to_string(reputation->success_rate) +
                             ", response_time_ms=" + std::to_string(reputation->avg_response_time_ms) +
                             ", feedback=" + std::to_string(reputation->feedback_score) + ")";
            return result;
        }
        result.status = ReputationCheckResult::Status::APPROVED;
        result.message = "reputation accepted";
    }

    for (const ProofType proof : endpoint.required_proofs) {
        if (proof == ProofType::NONE) {
            continue;
        }
        const auto outcome = blockchain_->verifyProofOfWork(wallet_address, proof, network, proof_hex, 1, now_unix);
        if (outcome.elapsed > kProofTimeout || !outcome.valid) {
            result.status = ReputationCheckResult::Status::PROOF_VALIDATION_FAILED;
            result.message = outcome.elapsed > kProofTimeout ? "proof validation exceeded 200ms" : outcome.detail;
            return result;
        }
    }
    return result;
}

void ReputationChecker::updateConfiguration(std::shared_ptr<Configuration> config) {
    if (!config) {
        throw std::invalid_argument("ReputationChecker configuration is empty");
    }
    std::lock_guard lock(config_mutex_);
    config_ = std::move(config);
}

std::shared_ptr<Configuration> ReputationChecker::configuration() const {
    std::lock_guard lock(config_mutex_);
    return config_;
}

}  // namespace gateway::core
