"""EIP-712 signer for the AgentGateway domain, version 1."""

from typing import Protocol


class Signer(Protocol):
    def sign(
        self,
        *,
        method: str,
        path: str,
        body: str,
        wallet: str,
        quote_reference: str,
        expiry_unix: int,
        chain_id: int,
        verifying_contract: str,
    ) -> str:
        """Return a 65-byte signature as 0x plus 130 hex characters."""


class EthAccountSigner:
    """Signs with eth-account. Install it with ``pip install eth-account``."""

    def __init__(self, private_key_hex: str) -> None:
        self._private_key_hex = private_key_hex

    def sign(
        self,
        *,
        method: str,
        path: str,
        body: str,
        wallet: str,
        quote_reference: str,
        expiry_unix: int,
        chain_id: int,
        verifying_contract: str,
    ) -> str:
        try:
            from eth_account import Account
            from eth_account.messages import encode_typed_data
            from eth_hash.auto import keccak
        except ImportError as error:
            raise ImportError("pip install eth-account") from error
        body_hash = keccak(body.encode("utf-8"))
        message = encode_typed_data(
            full_message={
                "types": {
                    "EIP712Domain": [
                        {"name": "name", "type": "string"},
                        {"name": "version", "type": "string"},
                        {"name": "chainId", "type": "uint256"},
                        {"name": "verifyingContract", "type": "address"},
                    ],
                    "Request": [
                        {"name": "method", "type": "string"},
                        {"name": "path", "type": "string"},
                        {"name": "bodyHash", "type": "bytes32"},
                        {"name": "wallet", "type": "address"},
                        {"name": "quoteRef", "type": "string"},
                        {"name": "expiry", "type": "uint256"},
                    ],
                },
                "primaryType": "Request",
                "domain": {
                    "name": "AgentGateway",
                    "version": "1",
                    "chainId": chain_id,
                    "verifyingContract": verifying_contract,
                },
                "message": {
                    "method": method,
                    "path": path,
                    "bodyHash": body_hash,
                    "wallet": wallet,
                    "quoteRef": quote_reference,
                    "expiry": expiry_unix,
                },
            }
        )
        signed = Account.sign_message(message, self._private_key_hex)
        return signed.signature.hex() if signed.signature.hex().startswith("0x") else "0x" + signed.signature.hex()
