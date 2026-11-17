# SynapseGateway 0.3.0

SynapseGateway is a C++20 reverse proxy for APIs called by autonomous agents. It answers HTTP 402 until the agent pays with a configured stablecoin, checks an ERC-8004 identity and reputation, then forwards the call to an existing backend. It is not a general-purpose API gateway. Kong, Envoy, and Tyk remain the right tools for routing, plugins, and a fleet of upstreams. This process is the payment and identity decision in front of one backend per route.

The sources are GPL-3.0-only. The executable is `SynapseGateway`. The EIP-712 domain signed by agents is `AgentGateway` version `1`. That string is the signature domain. Renaming it would invalidate signatures already issued.

## Run

```text
SynapseGateway config/gateway.dev.toml
```

With no argument the process reads `config/gateway.dev.toml`. `${NAME}` placeholders are replaced from the environment before parsing. An invalid file prints the error and exits 1.

```bash
docker compose up --build
```

The image listens on 8080 and exposes Prometheus on 9090. Compose also starts Prometheus and Grafana on ports 9091 and 3000. The first build compiles the C++ dependencies. A systemd unit lives in `deploy/agentgateway.service`.

On startup the process derives the collection address with CREATE2 from `payment_wallet_seed`. The same seed yields the same address on Ethereum, Base, Arbitrum, Polygon, and BNB Chain. The seed is not a spendable key. Gas and refunds use `blockchain.treasury_private_key`, a separate 32-byte key. State is stored in `data/payment-state.json`.

## Request path

`GET /health`, `GET /mcp`, and `POST /mcp` do not require a signature. Health returns 200 only when every enabled network is healthy, otherwise 503 with a per-network detail. `GET` or `POST /v1/auth` reads the original method and URI from the edge-proxy headers and then follows the list below. That path never calls the backend.

Every protected request follows this order.

1. **Signature.** `X-Agent-Signature` is an EIP-712 signature over method, path, body hash, wallet, quote reference, and quote expiry. The domain is `AgentGateway` version `1`, the chain id is the quote network, and the verifying contract is the CREATE2 collection address. If recovery does not match the wallet, the process calls `isValidSignature` (ERC-1271) and accepts the magic value `0x1626ba7e`. Anything else is 401.
2. **Wallet.** The address comes from `X-Agent-Wallet`, `X-Wallet-Address`, or `X-Payment-Wallet`. It must be `0x` plus 40 hex digits.
3. **Denylist.** A wallet in `[wallet_policy].denylist` is 403 before payment, refunds, or credits.
4. **Refunds and credit.** `POST /v1/refunds` and `POST /v1/credits` are handled here.
5. **Route.** The path is matched against `[[endpoints]]` regular expressions. Otherwise `[default_endpoint]` applies.
6. **Rate limit.** A 60-second window per wallet. Over the limit the response is 429 with `Retry-After`.
7. **Allowlist.** A non-empty endpoint `allowlist` rejects every other wallet with 403.
8. **BeforePayment filter.** An installed `GatewayFilter` may stop the request before a quote, a debit, or a chain lookup. An empty chain does nothing.
9. **Payment.** See below.
10. **Identity.** The ERC-721 registry of the selected network must assign an NFT to the wallet. The result is cached for 300 seconds. `minimum_nft_age_seconds` above zero requires the NFT registration timestamp to be at least that old. Failure is 403.
11. **Reputation.** The score is a weighted mean of success rate (0.5), response time (0.2), and feedback (0.3), clamped to `[0, 1]`. Below `trust_threshold` the response is 403. With no history, `new_agents_allowed` decides. Required zkML, TEE, or staking proofs are checked on the validation registry.
12. **AfterIdentity filter.** A denial here credits a prepaid debit back.
13. **Forward.** Payment headers, the signature, hop-by-hop headers, and bytes below `0x20` are removed. The gateway writes wallet, identity token, identity type, reputation, network, and request count. A `BeforeForward` filter may append headers or deny the call. Connect timeout is at most 5 seconds. The backend read timeout is 30 seconds. `/v1/auth` returns `{"allowed":true}` here and does not open a backend connection.
14. **Answer.** The backend status is returned with `X-Payment-Amount` and `X-Transaction-Hash`. A backend 5xx, a connection failure (502), or a read timeout (504) marks a chain payment refundable. A prepaid debit is credited back on identity, reputation, filter denial, or backend failure.

Header names that start with `X-Payment` or `X-402`, plus `X-Transaction-Hash` and `X-Agent-Signature`, are not forwarded.

## Payments

A price of zero does not touch the chain.

A positive price without `X-Payment-Tx` and without `X-Payment-Authorization` returns 402. The body carries the price, the collection wallet, the cheapest healthy network, `transaction_reference`, and `expires_at_unix`. The quote lives for 15 minutes and is stored on disk. The retry must send the same reference in `X-Payment-Reference`. The first call signs an empty reference and expiry 0, because the reference does not exist yet. The paid retry signs the issued reference and its expiry.

USDC is the default token (`usdc_contract`, `usdc_decimals`). `stablecoins` may add further contracts, such as USDT or EURC, each with its own decimals. The quoted number is scaled by the decimals of the token that was transferred. The transfer must come from the agent wallet and go to the CREATE2 address, for at least 99.9 percent of the quote. Native value and any other token do not count. If the same transaction contains USDC and another accepted stablecoin, USDC wins.

A verified hash is cached for 24 hours and pays only the bound path, price, wallet, and network. After that window the same hash is 409. A bloom filter plus an exact set implements the replay check. A bloom false positive does not reject a hash that the exact set does not contain.

`X-Payment-Authorization` is a USDC EIP-3009 `transferWithAuthorization`. The gateway checks the signature, broadcasts the call with the treasury key as an EIP-1559 transaction of value zero, and accepts the HTTP call only after the USDC receipt is finalized. The same nonce reuses the stored hash and does not broadcast again. Without a treasury key the authorization is refused. EIP-3009 collection is USDC only.

When `gas_budget_usdc` is strictly greater than `price_usdc`, the call debits a local prepaid balance instead of asking for a new transaction. A short balance returns 402 with `balance_usdc` and `top_up` set to `POST /v1/credits`. A deposit is a finalized transfer of an accepted stablecoin from the agent to the collection wallet. The same hash cannot also pay a call.

Finality is 12 confirmations on Ethereum mainnet and 1 confirmation on the L2 networks.

## Refunds

`POST /v1/refunds` takes `tx_hash`, `wallet_address`, and `gas_fee_usdc`. The gas figure is an explicit USDC amount. The gateway does not price native gas against USDC. The gas must be strictly below the payment. The recorded remainder is `amount - gas`.

With `treasury_private_key` set, the process signs an ERC-20 `transfer` of that remainder in USDC, broadcasts it, and sets `refund_processed` only after a finalized receipt to the user. A second call polls `refund_tx_hash` and does not send again. Without the key, the ledger closes locally and the transfer stays an external operation. The CREATE2 seed cannot send that transfer.

## Configuration

TOML is parsed, validated, and serialized. A round trip of a valid file is equivalent. The file is polled and a valid change replaces the active configuration, including the treasury key and the wallet policy. Networks, USDC, extra stablecoins, prices, allowlists, and the denylist all come from that file.

Production templates keep seeds, the treasury key, RPC URLs, and contract addresses in environment variables. Example and development files do not contain a treasury key.

## What the process already observes

Prometheus, when enabled, listens on `monitoring_port` and records request counts, payment amounts, verification latency, backend errors, cache hits, and network health. Security rejections include the wallet and the reason. Network health is refreshed every 10 seconds and gas every 60 seconds. A network that stops answering is left out of payment selection.

An OTLP span leaves the process only when `opentelemetry_enabled` is true and `otlp_endpoint` is an `http` or `https` URL. The flag defaults to false and the endpoint defaults to empty, so a stock configuration exports nothing. There is no warning when CPU or memory crosses 80 percent.

## Shutdown

SIGTERM, Ctrl+C, or closing the console stops new accepts. Requests already inside the process have up to 30 seconds. State is written, RPC clients are closed, and the process exits 0. A request that arrives while draining receives 503.

## Decision for another gateway

`GET` or `POST /v1/auth` runs the same payment, identity, and reputation checks and does not call the backend. A 200 body is `{"allowed":true}`. The response carries `X-Agent-Wallet`, the identity headers, `X-Payment-Amount`, and `X-Transaction-Hash`. Any other status is the same denial the agent would have received, including 402.

The original call is named by headers, because the edge proxy replaces the path:

| Proxy | Method header | URI header |
| --- | --- | --- |
| Traefik ForwardAuth | `X-Forwarded-Method` | `X-Forwarded-Uri` |
| nginx `auth_request` | `X-Original-Method` | `X-Original-URI` |
| Envoy ext_authz HTTP | `X-Original-Method` | `X-Original-URI` or `X-Envoy-Original-Path` |

Envoy does not add those headers by itself. Point `path_prefix` at `/v1/auth` and add them with `%REQ(:METHOD)%` and `%REQ(:PATH)%`. The body is part of the EIP-712 signature, so a POST decision has to forward the original body. nginx `auth_request` drops it unless the operator preserves it.

```yaml
http:
  middlewares:
    synapse:
      forwardAuth:
        address: http://synapsegateway:8080/v1/auth
        authResponseHeaders:
          - X-Agent-Wallet
          - X-Agent-Identity-Token
          - X-Agent-Reputation
          - X-Payment-Amount
```

```nginx
location = /synapse-auth {
    internal;
    proxy_pass http://synapsegateway:8080/v1/auth;
    proxy_set_header X-Original-URI $request_uri;
    proxy_set_header X-Original-Method $request_method;
    proxy_pass_request_body off;
    proxy_set_header Content-Length "";
}
```

Use `proxy_pass_request_body off` only for requests whose signature covers an empty body.

## Check and the collection address

```text
SynapseGateway check config/gateway.prod.toml
```

The command loads the file, expands environment placeholders, validates it, prints `configuration is valid` and `payment_wallet=0x…`, and exits. It does not open a port and it does not call a blockchain node. A failed check prints the errors on stderr and exits 1. The same address is written to the log when the process starts listening.

## Filters

`GatewayFilter` is the extension point. `GatewayRouter::addFilter` keeps the filter for the life of the process. `FilterChain` runs it at three phases:

- `BeforePayment`, after the allowlist and before a quote, a debit, or a chain lookup
- `AfterIdentity`, after reputation is approved
- `BeforeForward`, after the upstream headers exist and before the backend call

A filter that sets `continue_request` to false stops the request with its status and body. A prepaid debit made before `AfterIdentity` or `BeforeForward` is credited back. Headers appended to `FilterContext::added_headers` during `BeforeForward` are sent upstream. An empty chain leaves the built-in behavior unchanged.

## Trace export

`opentelemetry_enabled` defaults to false. A span is posted only when that flag is true and `otlp_endpoint` is an `http` or `https` URL. The body is one OTLP HTTP JSON span named `gateway.request`, with method, path, and status. Export failure does not change the agent response. An enabled flag with an empty endpoint logs a warning and exports nothing.

## MCP catalog

`GET /mcp` needs no signature. It returns the configured routes with path, price, trust threshold, required proofs, and the collection wallet. `POST /mcp` speaks a small slice of MCP JSON-RPC: `initialize`, `tools/list`, and `notifications/initialized`. Unknown methods return JSON-RPC `-32601`. The catalog is rebuilt from the configuration loaded for that request, so a reload is visible on the next call. The endpoint does not invoke the backend.

## Design gaps that are still open

Two items in older reviews are already in this version.

| Older claim | Version 0.2.0 |
| --- | --- |
| Refunds are ledger-only and the node never sends a transaction. | With a treasury key the node broadcasts the USDC refund and closes the ledger after the receipt. Without the key the ledger-only path remains, so existing configs keep working. |
| The agent signature is checked for shape only. | The signer is recovered locally. Contract wallets are checked with ERC-1271. |

These four requirements are real and are not implemented.

**Persistent RPC sessions.** Each JSON-RPC call resolves the host, opens a TCP or TLS connection, writes one request, and shuts the socket down. A semaphore limits how many calls are in flight. The HTTP header says `keep-alive`, but the socket is not reused. WebSocket URLs carry a single JSON-RPC frame. They are not subscriptions.

**Parallel verification and payment subscriptions.** Payment, identity, and reputation run one after another on the request thread. Requirement 10 also asks for `eth_subscribe` instead of receipt polling. The 402 response is still decided before identity work when the caller has not paid. A subscription listener would be a process-lifetime WebSocket per network. This version polls the receipt on the paid retry.

**Resource warning.** There is still no log when CPU or memory crosses 80 percent. The OTLP span above is the tracing part of requirement 12. The resource warning needs a platform file, and domain headers do not include operating-system APIs.

**MCP from OpenAPI.** `GET /mcp` and `tools/list` publish the gateway routes. They do not read an OpenAPI document from the backend and turn each backend operation into a tool. That remains requirement 9.7.

The backend connection pool, the 24-hour replay set, header sanitization, Prometheus, structured logs, and the 30-second drain are implemented. Release throughput of 1000 requests per second per core was not measured. The Debug floor exercised by the tests is 200 requests per second.
