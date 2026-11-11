# AgentGateway

AgentGateway is a high-performance C++20 middleware solution that bridges AI agents with traditional APIs through blockchain-based payment and identity verification.

## Features

- 🚀 **High Performance**: Sub-100ms verification latency, 1000+ req/s/core throughput
- 🔗 **Multi-Blockchain**: Ethereum, Base, Arbitrum, Polygon, BNB Chain support
- 💰 **x402 Payment Protocol**: Micropayment-based API monetization
- 🔐 **ERC-8004 Identity**: Blockchain-based agent identity verification
- 📊 **Built-in Monitoring**: Prometheus metrics, structured logging
- 🌐 **Cross-Platform**: Windows, Linux, macOS support

## Quick Start

### Prerequisites

- **C++20 Compiler**:
  - Windows: MSVC 2019 16.11+ or MSVC 2022
  - Linux: GCC 10+ or Clang 12+
  - macOS: Xcode Command Line Tools (Clang 12+)
- **CMake**: 3.20 or later
- **vcpkg**: For dependency management

### Installing vcpkg

**Windows:**
```powershell
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Get-Location).Path
```

**Linux/macOS:**
```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)
```

### Building the Project

**Automated Setup (Recommended):**

Windows:
```powershell
.\setup-build.ps1 -BuildType Release
cmake --build --preset windows-msvc-release
```

Linux/macOS:
```bash
chmod +x setup-build.sh
./setup-build.sh Release
cmake --build --preset linux-gcc-release  # or linux-clang-release / macos-clang-release
```

**Manual Setup:**

```bash
cmake --preset windows-msvc-release  # or linux-gcc-release, macos-clang-release
cmake --build --preset windows-msvc-release
```

### Running Tests

```bash
ctest --preset windows-msvc-release  # or appropriate preset for your platform
```

## Documentation

- [Dependencies](DEPENDENCIES.md) - Detailed dependency information and setup
- [Deployment](docs/deployment.md) - Build, Docker, systemd, and monitoring
- [API](docs/api.md) - x402 headers, status codes, and refunds
- [Funzionamento](docs/funzionamento.md) - avvio, percorso di una richiesta, rimborsi e arresto

## Project Structure

```
AgentGateway/
├── cmake/              # Custom CMake find modules
├── include/gateway/    # Public headers
│   ├── core/          # Core components (router, payment, identity)
│   ├── blockchain/    # Blockchain integration
│   ├── monitoring/    # Metrics and logging
│   └── utils/         # Utility classes
├── src/               # Implementation files
├── tests/             # Unit and integration tests
├── config/            # Example configuration files
└── doc/               # Documentation

```

## Dependencies

AgentGateway uses vcpkg for dependency management. Key dependencies include:

- **Drogon** (>= 1.9.0) - HTTP framework
- **Boost** (>= 1.75.0) - Networking and utilities
- **OpenSSL** (>= 3.0.0) - Cryptography
- **toml++** (>= 3.0.0) - Configuration parsing
- **spdlog** (>= 1.12.0) - Logging
- **nlohmann_json** (>= 3.11.0) - JSON processing
- **prometheus-cpp** (>= 1.2.0) - Metrics
- **Google Test** (>= 1.14.0) - Unit testing

Property tests run 100 iterations with the in-tree generator. RapidCheck is not part of the vcpkg baseline used by this tree.

See [DEPENDENCIES.md](DEPENDENCIES.md) for complete details.

## Configuration

Configuration is managed through TOML files. Example:

```toml
[server]
listen_address = "0.0.0.0"
listen_port = 8080

[blockchain]
payment_wallet_seed = "your-seed-here"

[[blockchain.networks]]
network = "base"
rpc_url = "https://mainnet.base.org"
enabled = true

[[endpoints]]
path_pattern = "/api/v1/.*"
price_usdc = 0.001
trust_threshold = 0.7
backend_url = "http://localhost:3000"
```

## Run

```text
gateway_node config/gateway.dev.toml
```

The process listens on the configured HTTP port and writes payment state to `data/payment-state.json`. SIGTERM drains in-flight requests and exits 0.

## Development Status

- [x] Build system and dependency management
- [x] Core utilities (cache, bloom filter, rate limiter)
- [x] Configuration management
- [x] Blockchain connector
- [x] Payment processor
- [x] Identity and reputation verification
- [x] HTTP gateway router
- [x] Monitoring and metrics
- [x] Integration tests, benchmarks, Docker, and systemd

## License

GNU General Public License v3.0 only. See [LICENSE.md](LICENSE.md).

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting pull requests.

## Support

For issues, questions, or contributions, please open an issue on GitHub.

---

# SynapseGateway
Agentic Gateway, Financial Identity for Agentic AI
