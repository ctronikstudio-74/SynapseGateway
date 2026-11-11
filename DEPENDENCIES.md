# AgentGateway Dependencies

This document describes all third-party dependencies used in the AgentGateway project and how to set them up.

## Dependency Management

AgentGateway uses **vcpkg** for cross-platform dependency management. vcpkg provides consistent package management across Windows, Linux, and macOS.

### Installing vcpkg

If you don't have vcpkg installed:

**Windows:**
```powershell
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

**Linux/macOS:**
```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
```

### Setting Up Environment

Set the `VCPKG_ROOT` environment variable:

**Windows:**
```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

**Linux/macOS:**
```bash
export VCPKG_ROOT=/path/to/vcpkg
```

## Required Dependencies

All dependencies are specified in `vcpkg.json` and will be automatically installed when you configure the project with CMake.

### Core Dependencies

#### 1. Drogon (>= 1.9.0)
- **Purpose**: High-performance asynchronous HTTP framework with coroutine support
- **License**: MIT
- **Features**: SSL support, ORM integration
- **Website**: https://github.com/drogonframework/drogon
- **Usage**: HTTP server, reverse proxy, request routing

#### 2. Boost (>= 1.75.0)
- **Purpose**: Collection of peer-reviewed portable C++ libraries
- **License**: Boost Software License
- **Components Used**:
  - `Boost.Asio`: Asynchronous I/O and networking
  - `Boost.Beast`: HTTP and WebSocket networking
  - `Boost.System`: System error handling
  - `Boost.Thread`: Threading primitives
  - `Boost.Filesystem`: File system operations
  - `Boost.JSON`: JSON parsing and serialization
- **Website**: https://www.boost.org
- **Usage**: Blockchain RPC client, async operations, JSON-RPC

#### 3. OpenSSL (>= 3.0.0)
- **Purpose**: Cryptography and SSL/TLS toolkit
- **License**: Apache License 2.0
- **Website**: https://www.openssl.org
- **Usage**: SHA-256 hashing, ECDSA signature verification, SSL/TLS connections, CREATE2 wallet generation

#### 4. toml++ (>= 3.0.0)
- **Purpose**: Header-only TOML v1.0.0 parser and serializer
- **License**: MIT
- **Website**: https://github.com/marzer/tomlplusplus
- **Usage**: Configuration file parsing (TOML format)

#### 5. spdlog (>= 1.12.0)
- **Purpose**: Fast C++ logging library
- **License**: MIT
- **Features**: fmt library integration, structured logging
- **Website**: https://github.com/gabime/spdlog
- **Usage**: Structured JSON logging, performance monitoring

#### 6. nlohmann_json (>= 3.11.0)
- **Purpose**: JSON for Modern C++
- **License**: MIT
- **Website**: https://github.com/nlohmann/json
- **Usage**: JSON parsing for blockchain responses, API payloads

#### 7. prometheus-cpp (>= 1.2.0)
- **Purpose**: Prometheus client library for C++
- **License**: MIT
- **Features**: Pull model for metrics exposition
- **Website**: https://github.com/jupp0r/prometheus-cpp
- **Usage**: Metrics collection and Prometheus integration

### Testing Dependencies

#### 8. Google Test (>= 1.14.0)
- **Purpose**: Google's C++ test framework
- **License**: BSD-3-Clause
- **Components**: GTest, GMock
- **Website**: https://github.com/google/googletest
- **Usage**: Unit testing, mock objects

#### 9. RapidCheck (latest)
- **Purpose**: Property-based testing library for C++
- **License**: BSD-2-Clause
- **Website**: https://github.com/emil-e/rapidcheck
- **Installation**: Automatically downloaded via CMake FetchContent
- **Usage**: Property-based testing for correctness properties
- **Note**: Not available in vcpkg; custom FindRapidCheck.cmake module handles installation

## Building the Project

### Using CMake Presets (Recommended)

CMake presets provide convenient configurations for different platforms:

**Windows (MSVC):**
```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

**Linux (GCC):**
```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

**Linux (Clang):**
```bash
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release
```

**macOS (AppleClang):**
```bash
cmake --preset macos-clang-release
cmake --build --preset macos-clang-release
```

### Manual Configuration

If you prefer manual configuration:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### First Build

On the first build, vcpkg will automatically:
1. Download all dependencies specified in `vcpkg.json`
2. Build them for your platform
3. Configure CMake to use them

This process may take 15-30 minutes depending on your system.

## Dependency Versions

The project uses semantic versioning constraints in `vcpkg.json`:
- **`version>=`**: Minimum version required, newer versions accepted
- **`builtin-baseline`**: Uses the latest stable versions from vcpkg

### Version Requirements Rationale

- **Boost 1.75+**: Required for `boost::json` support
- **OpenSSL 3.0+**: Required for modern cryptographic algorithms
- **Drogon 1.9+**: Required for stable coroutine support
- **GTest 1.14+**: Latest stable release with improved features

## Troubleshooting

### vcpkg Not Found
Ensure `VCPKG_ROOT` environment variable is set correctly and points to your vcpkg installation.

### Compilation Errors
1. Ensure you're using a C++20-compatible compiler:
   - MSVC 2019 16.11+ or MSVC 2022
   - GCC 10+
   - Clang 12+
2. Clear the build directory and reconfigure: `rm -rf build && cmake --preset <preset-name>`

### Dependency Download Failures
If vcpkg fails to download dependencies:
1. Check your internet connection
2. Update vcpkg: `git pull` in vcpkg directory, then re-run bootstrap script
3. Clear vcpkg cache: `vcpkg remove --outdated`

### RapidCheck Build Issues
RapidCheck is downloaded via CMake FetchContent. If it fails:
1. Check internet connectivity
2. Ensure Git is installed and accessible
3. Clear CMake cache and reconfigure

## Updating Dependencies

To update all dependencies to their latest compatible versions:

```bash
cd $VCPKG_ROOT
git pull
./vcpkg upgrade --no-dry-run
```

Then rebuild the project:

```bash
cmake --build build --target clean
cmake --build build
```

## Custom Find Modules

The project includes custom CMake Find modules in the `cmake/` directory:
- **FindRapidCheck.cmake**: Handles automatic download and configuration of RapidCheck

See `cmake/README.md` for details on custom modules.

## License Compliance

All dependencies are used under permissive licenses (MIT, BSD, Boost, Apache 2.0). Ensure compliance with each license when distributing the software.

## References

- [vcpkg Documentation](https://vcpkg.io/en/docs/README.html)
- [CMake Documentation](https://cmake.org/documentation/)
- [Dependency Licenses](./LICENSES/) (to be created in production)
