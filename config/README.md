# AgentGateway Configuration

This directory contains configuration files for the AgentGateway middleware system.

## Configuration Files

- **gateway.example.toml** - Complete configuration example with all available options and documentation
- **gateway.dev.toml** - Development configuration optimized for local testing
- **gateway.prod.toml** - Production configuration template with environment variable placeholders

## Quick Start

### Development

For local development, use the dev configuration:

```bash
# Copy the dev config
cp config/gateway.dev.toml config/gateway.toml

# Run AgentGateway
./build/bin/agentgateway --config config/gateway.toml
```

### Production

For production deployment:

1. Copy the production template:
   ```bash
   cp config/gateway.prod.toml config/gateway.toml
   ```

2. Set environment variables for sensitive data:
   ```bash
   export AGENTGW_WALLET_SEED="your-secure-seed-phrase"
   export BASE_RPC_URL="https://mainnet.base.org"
   export BASE_IDENTITY_REGISTRY="0x..."
   export BASE_REPUTATION_REGISTRY="0x..."
   export BASE_VALIDATION_REGISTRY="0x..."
   # ... set other required variables
   ```

3. Review and customize endpoint configurations

4. Run AgentGateway with the production config

## Configuration Sections

### [server]

HTTP server binding and thread pool configuration:
- `listen_address` - IP address to bind (0.0.0.0 for all interfaces)
- `listen_port` - HTTP port for API requests (default: 8080)
- `monitoring_port` - Prometheus metrics port (default: 9090)
- `thread_pool_size` - Worker threads (0 = auto-detect)

### [blockchain]

Blockchain network configuration and wallet settings:
- `payment_wallet_seed` - BIP39 seed phrase for deterministic wallet generation
- `networks[]` - Array of blockchain network configurations:
  - `network` - Network identifier (base, arbitrum, polygon, ethereum, bnb)
  - `rpc_url` - JSON-RPC endpoint URL
  - `identity_registry` - ERC-721 identity registry contract address
  - `reputation_registry` - On-chain reputation registry contract address
  - `validation_registry` - Proof-of-work validation contract address
  - `gas_limit` - Maximum gas limit for transactions
  - `enabled` - Whether network is active

### [[endpoints]]

Per-endpoint configuration for API monetization:
- `path_pattern` - Regex pattern for matching request paths
- `price_usdc` - Price in USDC (0.0 = free, no blockchain verification)
- `trust_threshold` - Minimum reputation score (0.0 - 1.0)
- `required_proofs` - Array of required proof types: none, zkml, tee, staking
- `backend_url` - Target backend API URL
- `backend_timeout` - Backend request timeout in seconds
- `rate_limit_per_minute` - Maximum requests per wallet per minute
- `new_agents_allowed` - Allow agents without reputation history

### [default_endpoint]

Default configuration applied when no endpoint pattern matches.

### [cache]

In-memory cache configuration:
- `max_size_mb` - Maximum cache size in megabytes
- `default_ttl_seconds` - Default time-to-live for cached entries

### [security]

Security and replay attack prevention:
- `bloom_filter_size_mb` - Bloom filter size for transaction deduplication
- `bloom_filter_retention_hours` - Transaction history retention period

### [monitoring]

Observability and logging configuration:
- `prometheus_enabled` - Enable Prometheus metrics exposition
- `opentelemetry_enabled` - Enable OpenTelemetry distributed tracing
- `otel_collector_endpoint` - OpenTelemetry collector URL (if enabled)
- `log_level` - Logging verbosity (trace, debug, info, warn, error)
- `json_logging` - Use structured JSON log format

## Environment Variables

Sensitive configuration values should be provided via environment variables:

- `AGENTGW_WALLET_SEED` - Payment wallet seed phrase
- `${NETWORK}_RPC_URL` - RPC endpoint for each blockchain network
- `${NETWORK}_IDENTITY_REGISTRY` - Identity registry contract addresses
- `${NETWORK}_REPUTATION_REGISTRY` - Reputation registry contract addresses
- `${NETWORK}_VALIDATION_REGISTRY` - Validation registry contract addresses
- `BACKEND_*_URL` - Backend API URLs for different tiers

## Hot Reload

AgentGateway watches the configuration file for changes and automatically reloads within 10 seconds. Configuration updates apply immediately to all requests, including in-flight requests.

To force a reload, send `SIGHUP` signal:
```bash
kill -HUP $(pidof agentgateway)
```

## Security Best Practices

1. **Never commit secrets** - Use environment variables or secret management systems
2. **Rotate wallet seeds** - Periodically generate new wallet seeds for payment collection
3. **Use HTTPS RPC endpoints** - Ensure blockchain connections are encrypted
4. **Review rate limits** - Adjust rate limits based on your API capacity
5. **Monitor bloom filter size** - Ensure sufficient memory for replay attack prevention
6. **Enable authentication** - Use request signing for agent identity verification
7. **Audit endpoint pricing** - Regularly review and adjust USDC pricing
8. **Test configuration changes** - Validate configuration syntax before deployment

## Troubleshooting

### Configuration Validation Errors

Run configuration validation before starting:
```bash
./build/bin/agentgateway --config config/gateway.toml --validate
```

### Log Configuration Details

Enable debug logging to see active configuration:
```bash
# Set log_level = "debug" in config
./build/bin/agentgateway --config config/gateway.toml
```

### Common Issues

- **RPC connection failures** - Verify RPC URLs are accessible
- **Contract not found** - Ensure registry contract addresses are correct
- **Rate limiting too strict** - Increase `rate_limit_per_minute` values
- **Cache misses** - Increase `max_size_mb` or `default_ttl_seconds`
- **High latency** - Check blockchain network health and RPC performance

## Additional Resources

- [AgentGateway Documentation](../README.md)
- [x402 Payment Protocol Specification](../doc/)
- [ERC-8004 Identity Standard](../doc/)
- [TOML v1.0.0 Specification](https://toml.io/en/v1.0.0)
