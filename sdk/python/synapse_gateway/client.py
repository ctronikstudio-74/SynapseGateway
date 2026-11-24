"""HTTP client for health, catalog, paid calls, credits, refunds, and edge decisions."""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from typing import Callable
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from synapse_gateway.signer import Signer

CHAIN_IDS = {
    "ethereum_mainnet": 1,
    "ethereum": 1,
    "base": 8453,
    "arbitrum": 42161,
    "polygon": 137,
    "bnb_chain": 56,
    "bnb": 56,
}


@dataclass
class Quote:
    price_usdc: float = 0.0
    balance_usdc: float = 0.0
    payment_wallet: str = ""
    network: str = ""
    transaction_reference: str = ""
    expires_at_unix: int = 0
    top_up: str = ""


@dataclass
class Payment:
    tx_hash: str = ""
    authorization_json: str = ""


@dataclass
class CallResult:
    status: int
    body: str
    headers: dict[str, str] = field(default_factory=dict)
    quote: Quote | None = None

    def header(self, name: str) -> str:
        wanted = name.lower()
        for key, value in self.headers.items():
            if key.lower() == wanted:
                return value
        return ""


class UrllibTransport:
    def exchange(self, method: str, url: str, body: str, headers: list[tuple[str, str]]) -> CallResult:
        data = body.encode("utf-8") if body else None
        request = Request(url, data=data, method=method)
        for name, value in headers:
            request.add_header(name, value)
        try:
            with urlopen(request, timeout=30) as response:
                raw = response.read().decode("utf-8")
                return CallResult(response.status, raw, dict(response.headers.items()))
        except HTTPError as error:
            raw = error.read().decode("utf-8")
            return CallResult(error.code, raw, dict(error.headers.items()))


class Client:
    def __init__(
        self,
        base_url: str,
        wallet: str,
        signer: Signer,
        verifying_contract: str,
        chain_id: int = 8453,
        transport=None,
    ) -> None:
        if not base_url or not wallet or not verifying_contract or chain_id <= 0:
            raise ValueError("base URL, wallet, collection address, and chain id are required")
        self.base_url = base_url.rstrip("/")
        self.wallet = wallet
        self.signer = signer
        self.verifying_contract = verifying_contract
        self.chain_id = chain_id
        self.transport = transport or UrllibTransport()

    def health(self) -> CallResult:
        return self._exchange("GET", "/health", "", [], signed=False)

    def catalog(self) -> CallResult:
        return self._exchange("GET", "/mcp", "", [], signed=False)

    def mcp(self, method: str, params: dict | None = None, request_id: str = "1") -> CallResult:
        payload: dict = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        if method != "notifications/initialized":
            payload["id"] = request_id
        return self._exchange("POST", "/mcp", json.dumps(payload), [], signed=False)

    def call(
        self,
        method: str,
        path: str,
        body: str = "",
        *,
        tx_hash: str = "",
        authorization_json: str = "",
        proof: str = "",
        reference: str = "",
        expiry_unix: int = 0,
        chain_id: int = 0,
    ) -> CallResult:
        return self._exchange(
            method,
            path,
            body,
            [],
            signed=True,
            tx_hash=tx_hash,
            authorization_json=authorization_json,
            proof=proof,
            reference=reference,
            expiry_unix=expiry_unix,
            chain_id=chain_id or self.chain_id,
        )

    def invoke(self, method: str, path: str, body: str, payer: Callable[[Quote], Payment]) -> CallResult:
        first = self.call(method, path, body)
        if first.status != 402 or first.quote is None or not first.quote.transaction_reference:
            return first
        payment = payer(first.quote)
        if not payment.tx_hash and not payment.authorization_json:
            return first
        chain = CHAIN_IDS.get(first.quote.network, self.chain_id)
        return self.call(
            method,
            path,
            body,
            tx_hash=payment.tx_hash,
            authorization_json=payment.authorization_json,
            reference=first.quote.transaction_reference,
            expiry_unix=first.quote.expires_at_unix,
            chain_id=chain,
        )

    def deposit(self, tx_hash: str) -> CallResult:
        return self.call("POST", "/v1/credits", json.dumps({"tx_hash": tx_hash}))

    def refund(self, tx_hash: str, wallet_address: str, gas_fee_usdc: float) -> CallResult:
        payload = {"tx_hash": tx_hash, "wallet_address": wallet_address, "gas_fee_usdc": gas_fee_usdc}
        return self.call("POST", "/v1/refunds", json.dumps(payload))

    def decide(self, method: str, path: str, body: str = "", proxy: str = "nginx") -> CallResult:
        signature = self._sign(method, path, body, "", 0, self.chain_id)
        headers = [("X-Agent-Signature", signature), ("X-Agent-Wallet", self.wallet)]
        kind = proxy.lower()
        if kind == "traefik":
            headers += [("X-Forwarded-Method", method), ("X-Forwarded-Uri", path)]
        elif kind == "envoy":
            headers += [("X-Original-Method", method), ("X-Envoy-Original-Path", path)]
        else:
            headers += [("X-Original-Method", method), ("X-Original-URI", path)]
        return self._exchange("POST", "/v1/auth", body, headers, signed=False)

    def _sign(self, method: str, path: str, body: str, reference: str, expiry: int, chain_id: int) -> str:
        fields = {
            "method": method,
            "path": path,
            "body": body,
            "wallet": self.wallet,
            "quote_reference": reference,
            "expiry_unix": 0 if not reference else expiry,
            "chain_id": chain_id,
            "verifying_contract": self.verifying_contract,
        }
        if callable(self.signer) and not hasattr(self.signer, "sign"):
            return self.signer(**fields)
        return self.signer.sign(**fields)

    def _exchange(
        self,
        method: str,
        path: str,
        body: str,
        headers: list[tuple[str, str]],
        *,
        signed: bool,
        tx_hash: str = "",
        authorization_json: str = "",
        proof: str = "",
        reference: str = "",
        expiry_unix: int = 0,
        chain_id: int = 0,
    ) -> CallResult:
        outbound = list(headers)
        if signed:
            signature = self._sign(method, path, body, reference, expiry_unix, chain_id or self.chain_id)
            outbound += [("X-Agent-Signature", signature), ("X-Agent-Wallet", self.wallet)]
            if tx_hash:
                outbound.append(("X-Payment-Tx", tx_hash))
            if reference:
                outbound.append(("X-Payment-Reference", reference))
            if authorization_json:
                outbound.append(("X-Payment-Authorization", authorization_json))
            if proof:
                outbound.append(("X-Agent-Proof", proof))
        if body:
            outbound.append(("Content-Type", "application/json"))
        url = path if path.startswith("http://") or path.startswith("https://") else self.base_url + (path if path.startswith("/") else "/" + path)
        result = self.transport.exchange(method, url, body, outbound)
        if result.status == 402:
            result.quote = _quote(result.body, self.verifying_contract)
        return result


def _quote(body: str, verifying_contract: str) -> Quote | None:
    try:
        parsed = json.loads(body)
    except json.JSONDecodeError:
        return None
    if not isinstance(parsed, dict):
        return None
    quote = Quote(
        price_usdc=float(parsed.get("price_usdc", 0) or 0),
        balance_usdc=float(parsed.get("balance_usdc", 0) or 0),
        payment_wallet=str(parsed.get("payment_wallet", "")),
        network=str(parsed.get("network", "")),
        transaction_reference=str(parsed.get("transaction_reference", "")),
        expires_at_unix=int(parsed.get("expires_at_unix", 0) or 0),
        top_up=str(parsed.get("top_up", "")),
    )
    if quote.payment_wallet and quote.payment_wallet.lower() != verifying_contract.lower():
        raise ValueError("quote collection address does not match the client verifying contract")
    return quote
