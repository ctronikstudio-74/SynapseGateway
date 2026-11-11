# AgentGateway HTTP API

AgentGateway is an x402 reverse proxy. An agent calls a protected path, receives HTTP 402 with the USDC price and payment wallet, pays on the selected L2, then retries with the transaction hash.

## Request headers

| Header | Required | Meaning |
| --- | --- | --- |
| `X-Agent-Signature` | yes, except `GET /health` | Agent signature token, 64 to 512 characters, hex or base64. Missing or malformed signatures return HTTP 401. |
| `X-Agent-Wallet` | yes for paid and identity checks | Checksum-free `0x` address with 40 hex digits. `X-Wallet-Address` is also accepted. |
| `X-Payment-Tx` | required when `price_usdc > 0` | Transaction hash of the USDC transfer to the gateway wallet. |
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
  "payment_header": "X-Payment-Tx"
}
```

`price_usdc` is decimal USDC. On-chain amounts are USDC base units (6 decimals). The gateway accepts a payment at or above 99.9% of the quote.

## Successful call

```bash
curl -i http://127.0.0.1:8080/api/v1/chat \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222" \
  -H "X-Payment-Tx: 0xabc..."
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
| 200 | Health is good, or the backend succeeded. |
| 401 | Signature or wallet is missing. |
| 402 | Payment is missing, pending, short, or invalid. |
| 403 | Identity NFT or reputation check failed. |
| 409 | Transaction hash was already used after the 24 hour cache, or a refund was blocked. |
| 429 | Per-wallet sliding window is full. `Retry-After` is in seconds. |
| 502 | The backend connection failed. A verified payment is marked refundable. |
| 503 | Health check failed, or the process is draining. |
| 504 | The backend did not answer within 30 seconds. The payment is marked refundable. |

Backend HTTP 5xx responses are forwarded as-is and the payment is marked refundable. Connection failure is always 502.

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

The refund is stored when `gas_fee_usdc` is strictly below the payment. The recorded refund amount is `amount_usdc - gas_fee_usdc`. Gas is supplied in USDC because the gateway does not price native gas against USDC.

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
