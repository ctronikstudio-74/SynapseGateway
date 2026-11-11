# Deployment

AgentGateway is C++20 and builds with CMake and vcpkg on Windows, Linux, and macOS. The process listens on the configured HTTP port and exposes Prometheus on the monitoring port.

## Build

Install a C++20 compiler, CMake 3.20 or newer, and vcpkg. Set `VCPKG_ROOT` to the vcpkg checkout, then configure with a preset from `CMakePresets.json`:

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release
```

Windows Debug against an existing Visual Studio cache:

```powershell
$env:VCPKG_ROOT = "D:\CMake\vcpkg"
cmake --build build/windows-msvc --config Debug
ctest --test-dir build/windows-msvc -C Debug --output-on-failure
```

The binary is `gateway_node`. It loads a TOML file, default `config/gateway.dev.toml`:

```bash
./gateway_node config/gateway.example.toml
```

`${NAME}` placeholders in the TOML are replaced from the environment before parsing. Unset names are left in place and fail validation. Production templates live in `config/gateway.prod.toml`. Keep `payment_wallet_seed` in `AGENTGW_WALLET_SEED` rather than in the file that you commit.

Runtime payment and refund state is written to `data/payment-state.json`, relative to the working directory.

## Configuration practice

- `config/gateway.example.toml` documents every field.
- `config/gateway.dev.toml` is a local free-tier file.
- `config/gateway.prod.toml` expects RPC URLs, registry addresses, and backend URLs from the environment.
- Addresses are `0x` plus 40 hex digits. Trust scores are in `[0, 1]`. Prices are non-negative USDC.
- A file change is polled about once a second and applied to requests already in flight after validation succeeds. A partial write is retried.
- Health probes run every 10 seconds so a dead RPC is dropped within the 30 second budget. Gas is refreshed every 60 seconds.

## RPC providers

Each enabled network needs an `http`, `https`, `ws`, or `wss` JSON-RPC endpoint. The connector uses TLS peer verification on `https` and `wss`. Public endpoints are enough for a trial. Production should use a provider with a dedicated key and, where possible, a local or private node so identity reads stay inside the latency budget.

Contract fields:

- `identity_registry` — ERC-721 identity registry
- `reputation_registry` — reputation registry
- `validation_registry` — zkML, TEE, and stake checks

The same CREATE2 seed produces the same payment wallet on Ethereum, Base, Arbitrum, Polygon, and BNB Chain.

## Process supervisor

`deploy/agentgateway.service` is a systemd unit. Install the binary to `/usr/local/bin/gateway_node`, the config to `/etc/agentgateway/gateway.toml`, and the state directory `/var/lib/agentgateway`.

```bash
sudo systemctl enable --now agentgateway
sudo journalctl -u agentgateway -f
```

The unit restarts on failure. SIGTERM stops new connections, waits up to 30 seconds for in-flight requests, writes `payment-state.json`, and exits 0.

## Docker

```bash
docker compose up --build
```

The image is a multi-stage Ubuntu 22.04 build. The runtime listens on 8080 and 9090. Compose also starts Prometheus and Grafana. Grafana is on port 3000 with the Prometheus datasource already provisioned. Scrape `agentgateway:9090`.

## Monitoring

Prometheus metrics include request counts, payment amounts, verification latency, backend errors, cache hits, and network health. Point Grafana at Prometheus using `deploy/grafana/provisioning`. Logs on stdout are JSON objects with `event` of `request` or `security`.

`GET /health` is the process probe. It returns 200 only when every enabled network answers the health flag, otherwise 503 with the per-network detail.

## Tests that ship with the source

Property checks use the in-tree 100-iteration runner in `tests/support/property.h`. RapidCheck is not part of the vcpkg baseline. Integration tests speak HTTP to the real Drogon server and to an in-process JSON-RPC mock, so CI does not need Hardhat. Optional Solidity stubs live under `tests/contracts/` for teams that want to deploy the registries themselves. Throughput numbers are measured in-process; `tests/perf/README.md` shows the `wrk` and `ab` commands for a running server. The 1000 request/s/core bar is enforced for Release builds. Debug builds assert a lower floor because they keep checks and skip inlining.
