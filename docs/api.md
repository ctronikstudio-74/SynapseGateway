# SynapseGateway HTTP API

Version 0.3.0. SynapseGateway is an x402 reverse proxy. An agent calls a protected path, receives HTTP 402 with the price and payment wallet, pays on the selected network, then retries with the transaction hash. The EIP-712 domain is `AgentGateway` version `1`.

## Request headers

| Header | Required | Meaning |
| --- | --- | --- |
| `X-Agent-Signature` | yes, except `GET /health` and `GET` or `POST /mcp` | 65-byte EIP-712 signature (`r`, `s`, `v`) over method, path, body hash, wallet, quote reference, and quote expiry. Contract wallets may send an ERC-1271 signature. A missing or non-matching signature returns HTTP 401. `/v1/auth` requires the signature on the original request. |
| `X-Agent-Wallet` | yes for paid and identity checks | Checksum-free `0x` address with 40 hex digits. `X-Wallet-Address` is also accepted. |
| `X-Payment-Tx` | required when `price_usdc > 0` and the call is not prepaid | Transaction hash of a USDC, or other configured stablecoin, transfer to the gateway wallet. |
| `X-Agent-Proof` | when the endpoint requires zkML, TEE, or staking | Proof bytes forwarded to the validation registry. |

Within 24 hours a verified hash is served from cache. After that window the same hash is rejected with HTTP 409.

## Payment quote

`GET /api/v1/chat` without `X-Payment-Tx` on a priced route:

```bash
curl -i http://127.0.0.1:8080/api/v1/chat \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222"
```

HTTP 402 body:

```json
{
  "error": "payment required",
  "price_usdc": 0.001,
  "payment_wallet": "0x...",
  "network": "base",
  "transaction_reference": "txref-1-...",
  "expires_at_unix": 1700000000,
  "payment_header": "X-Payment-Tx",
  "quote_header": "X-Payment-Reference"
}
```

`price_usdc` is the numeric price. USDC is the default token. A network may also list USDT or EURC under `stablecoins`, each with its own `decimals`. The transfer must be of one of those contracts, from the agent wallet, for at least 99.9% of the quote after scaling by that token's decimals. When both USDC and another stablecoin appear in the same transaction, USDC is the one that settles. The quote expires 15 minutes after it is issued. The retry must send the same `transaction_reference` in `X-Payment-Reference`. That hash then pays only this path, price, wallet, and network, including while it remains in the 24-hour cache. EIP-3009 collection and on-chain refunds use USDC only.

`X-Payment-Authorization` is an alternative to `X-Payment-Tx`. Its JSON body is a USDC EIP-3009 authorization: `to`, `value` (base-unit decimal), `valid_after`, `valid_before`, `nonce`, and `signature`. The gateway checks the signature, broadcasts `transferWithAuthorization` with the treasury key, and accepts the call only after the USDC receipt is finalized. The same nonce retries the stored hash and does not broadcast again. Without `blockchain.treasury_private_key` the authorization is refused.

When an endpoint sets `gas_budget_usdc` strictly above `price_usdc`, a call debits the wallet's prepaid balance instead of requiring a new transaction. A short balance returns 402 with `balance_usdc` and `top_up` set to `POST /v1/credits`. A deposit is `POST /v1/credits` with `{"tx_hash":"0x…"}`: a finalized USDC transfer from the agent to the collection wallet. The same hash cannot pay a call afterwards. Identity, reputation, and backend failures credit the price back.

## Successful call

```bash
curl -i http://127.0.0.1:8080/api/v1/chat \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222" \
  -H "X-Payment-Tx: 0xabc..." \
  -H "X-Payment-Reference: txref-1-..."
```

The backend receives the original headers except payment headers, hop-by-hop headers, and control characters. The gateway adds:

- `X-Agent-Wallet`
- `X-Agent-Identity-Token`
- `X-Agent-Identity-Type`
- `X-Agent-Reputation`
- `X-Agent-Network`
- `X-Agent-Requests`

The agent response gains `X-Payment-Amount` and `X-Transaction-Hash`.

## Status codes

| Code | When |
| --- | --- |
| 200 | Health is good, the backend succeeded, or `/v1/auth` allowed the call. |
| 400 | `/v1/auth` is missing `X-Original-URI` and `X-Original-Method` (or their ForwardAuth aliases), or a JSON body is not an object the route accepts. |
| 401 | Signature or wallet is missing. |
| 402 | Payment is missing, pending, short, or invalid. |
| 403 | The wallet is denied, it is outside the endpoint allowlist, the identity NFT is missing or newer than `minimum_nft_age_seconds`, the reputation check failed, or a filter stopped the request. |
| 409 | Transaction hash was already used after the 24 hour cache, or a refund was blocked. |
| 429 | Per-wallet sliding window is full. `Retry-After` is in seconds. |
| 502 | The backend connection failed. A verified payment is marked refundable. |
| 503 | Health check failed, or the process is draining. |
| 504 | The backend did not answer within 30 seconds. The payment is marked refundable. |

Backend HTTP 5xx responses are forwarded as-is and the payment is marked refundable. Connection failure is always 502.

## Decision endpoint

`GET` or `POST /v1/auth` is the hook for Envoy ext_authz, Traefik ForwardAuth, and nginx `auth_request`. Send the original method in `X-Original-Method` or `X-Forwarded-Method`, and the original path in `X-Original-URI`, `X-Forwarded-Uri`, or `X-Envoy-Original-Path`. The original agent headers, including the signature, are required. A 200 body is `{"allowed":true}` and the identity headers are on the response. The backend is not called. Any other status is the denial the agent would have received. A POST whose signature covers a body must forward that body.

## MCP catalog

`GET /mcp` does not require a signature. The JSON lists each configured route, its price, trust threshold, proofs, and the collection wallet. `POST /mcp` accepts MCP JSON-RPC `initialize`, `tools/list`, and `notifications/initialized`.

## SDK

The same calls are wrapped for C++, Python, TypeScript, Java, C#, and Lua in [sdk/README.md](../sdk/README.md). Paid support and the enterprise distribution are listed at <https://c-tronik.com/products/>, next to SynapseMFT and Synapse Firewall.

## Check

`SynapseGateway check config/gateway.toml` validates the file and prints the CREATE2 collection address. It does not listen.

## Health and metrics

- `GET /health` returns JSON. HTTP 200 when every enabled network is healthy, otherwise HTTP 503.
- Prometheus metrics listen on `monitoring_port` (default 9090), not on the proxy port.

## Refunds

```bash
curl -s -X POST http://127.0.0.1:8080/v1/refunds \
  -H "Content-Type: application/json" \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222" \
  -d '{"tx_hash":"0xabc","wallet_address":"0x2222222222222222222222222222222222222222","gas_fee_usdc":0.0001}'
```

The refund is stored when `gas_fee_usdc` is strictly below the payment. The recorded refund amount is `amount_usdc - gas_fee_usdc`. Gas is supplied in USDC because the gateway does not price native gas against USDC. When `blockchain.treasury_private_key` is set, the gateway broadcasts an ERC-20 `transfer` of that remainder and marks the refund processed only after the receipt is finalized. The stored `refund_tx_hash` is polled on retry. Without the key, the ledger closes locally and the transfer stays an external operation.

```python
import urllib.request

request = urllib.request.Request(
    "http://127.0.0.1:8080/health",
    method="GET",
)
print(urllib.request.urlopen(request).read().decode())
```

## Header safety

Values forwarded upstream have CR, LF, and other characters below 0x20 removed. Header names that are not RFC 7230 tokens are dropped. Names beginning with `X-Payment` or `X-402`, plus `X-Transaction-Hash` and `X-Agent-Signature`, are not forwarded.
