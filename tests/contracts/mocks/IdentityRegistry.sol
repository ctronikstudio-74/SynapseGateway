// SPDX-License-Identifier: GPL-3.0-only
pragma solidity ^0.8.20;

/// Minimal identity registry surface read by AgentGateway.
contract IdentityRegistry {
    struct Identity {
        string identityType;
        uint64 registeredAt;
    }

    mapping(uint256 => address) public ownerOfToken;
    mapping(address => uint256[]) public owned;
    mapping(uint256 => Identity) public identities;

    function mint(address agent, string calldata identityType) external returns (uint256 tokenId) {
        tokenId = owned[agent].length + 1;
        ownerOfToken[tokenId] = agent;
        owned[agent].push(tokenId);
        identities[tokenId] = Identity(identityType, uint64(block.timestamp));
    }

    function balanceOf(address agent) external view returns (uint256) {
        return owned[agent].length;
    }

    function tokenOfOwnerByIndex(address agent, uint256 index) external view returns (uint256) {
        return owned[agent][index];
    }

    function getIdentityData(uint256 tokenId) external view returns (string memory, uint64) {
        Identity memory identity = identities[tokenId];
        return (identity.identityType, identity.registeredAt);
    }
}
