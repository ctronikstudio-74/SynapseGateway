// SPDX-License-Identifier: GPL-3.0-only
pragma solidity ^0.8.20;

/// Reputation stored in basis points, matching the gateway ABI decoder.
contract ReputationRegistry {
    struct Score {
        uint256 successRateBps;
        uint256 avgResponseTimeMs;
        uint256 feedbackBps;
        uint256 totalRequests;
    }

    mapping(address => Score) public scores;

    function setScore(address agent, uint256 successRateBps, uint256 avgResponseTimeMs, uint256 feedbackBps, uint256 totalRequests) external {
        scores[agent] = Score(successRateBps, avgResponseTimeMs, feedbackBps, totalRequests);
    }

    function getReputation(address agent) external view returns (uint256, uint256, uint256, uint256) {
        Score memory score = scores[agent];
        return (score.successRateBps, score.avgResponseTimeMs, score.feedbackBps, score.totalRequests);
    }
}
