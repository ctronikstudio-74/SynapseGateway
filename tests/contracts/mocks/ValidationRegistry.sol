// SPDX-License-Identifier: GPL-3.0-only
pragma solidity ^0.8.20;

contract ValidationRegistry {
    mapping(address => uint256) public stakeOf;
    uint256 public constant minStake = 1;

    function verifyZkMLProof(address, bytes calldata proof) external pure returns (bool) {
        return proof.length > 0;
    }

    function verifyTEEAttestation(address, bytes calldata proof, uint256 attestedAt) external view returns (bool) {
        return proof.length > 0 && block.timestamp - attestedAt <= 7 days;
    }

    function setStake(address agent, uint256 amount) external {
        stakeOf[agent] = amount;
    }

    function getStake(address agent) external view returns (uint256) {
        return stakeOf[agent];
    }
}
