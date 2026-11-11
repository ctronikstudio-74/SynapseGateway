# AgentGateway Test Suite

This directory contains the test suite for the AgentGateway project, organized by test type and component.

## Directory Structure

```
tests/
├── core/              # Unit tests for core components
│   ├── gateway_router_test.cpp
│   ├── payment_processor_test.cpp
│   ├── identity_verifier_test.cpp
│   ├── reputation_checker_test.cpp
│   └── configuration_test.cpp
├── blockchain/        # Unit tests for blockchain components
│   ├── blockchain_connector_test.cpp
│   └── network_client_test.cpp
├── integration/       # Integration tests
│   ├── end_to_end_test.cpp
│   ├── payment_flow_test.cpp
│   └── multi_network_test.cpp
└── properties/        # Property-based tests
    ├── payment_properties_test.cpp
    ├── cache_properties_test.cpp
    └── config_properties_test.cpp
```

## Test Types

### Unit Tests

Unit tests verify individual components in isolation using mocked dependencies.

**Location**: `core/` and `blockchain/` directories

**Framework**: Catch2 v3

**Example**:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gateway/core/payment_processor.hpp"

TEST_CASE("PaymentProcessor verifies valid transactions", "[payment]") {
    // Test implementation
}
```

**Running unit tests**:
```bash
# Run all unit tests
./build/bin/agentgateway_tests

# Run specific test file
./build/bin/agentgateway_tests core/payment_processor_test.cpp

# Run tests matching a pattern
./build/bin/agentgateway_tests -t "[payment]"
```

### Integration Tests

Integration tests verify component interactions with real or simulated external systems.

**Location**: `integration/` directory

**Dependencies**: Requires test blockchain node and mock backend API

**Running integration tests**:
```bash
# Start test infrastructure
docker-compose -f tests/docker-compose.test.yml up -d

# Run integration tests
./build/bin/agentgateway_tests integration/

# Cleanup
docker-compose -f tests/docker-compose.test.yml down
```

### Property-Based Tests

Property-based tests verify system invariants across randomly generated inputs.

**Location**: `properties/` directory

**Framework**: RapidCheck

**Example**:
```cpp
#include <rapidcheck.h>
#include "gateway/core/cache.hpp"

RC_GTEST_PROP(CacheProperties, LRUEvictionOrder, ()) {
    // Property test implementation
}
```

**Running property tests**:
```bash
# Run with default 100 iterations per property
./build/bin/agentgateway_tests properties/

# Run with more iterations for thorough testing
./build/bin/agentgateway_tests properties/ --rng-seed=time --rc-runs=1000
```

## Test Coverage

### Coverage Requirements

- **Unit test coverage**: Minimum 80% line coverage for core components
- **Branch coverage**: Minimum 70% for decision logic
- **Integration coverage**: All critical user flows tested end-to-end

### Generating Coverage Reports

```bash
# Configure with coverage enabled
cmake --preset coverage

# Build tests with coverage instrumentation
cmake --build --preset coverage

# Run tests
./build-coverage/bin/agentgateway_tests

# Generate coverage report
cmake --build --preset coverage --target coverage
```

Coverage report available at: `build-coverage/coverage/index.html`

## Test Conventions

### Naming

- Test files: `{component}_test.cpp`
- Test cases: Descriptive sentences starting with capital letter
- Test tags: `[component]`, `[integration]`, `[property]`

### Organization

```cpp
// Test file structure
#include <catch2/catch_test_macros.hpp>
#include "component_under_test.hpp"
#include "test_helpers.hpp"

namespace gateway::test {

// Helper functions and fixtures
class ComponentFixture { /* ... */ };

// Test cases grouped by functionality
TEST_CASE("Component handles valid input", "[component]") {
    SECTION("with default configuration") { /* ... */ }
    SECTION("with custom configuration") { /* ... */ }
}

TEST_CASE("Component rejects invalid input", "[component]") {
    // Test implementation
}

} // namespace gateway::test
```

### Assertions

Use Catch2 assertion macros:
```cpp
REQUIRE(value == expected);           // Test fails if false
CHECK(value == expected);             // Test continues if false
REQUIRE_THROWS_AS(expr, Exception);   // Verify exception type
REQUIRE_NOTHROW(expr);                // Verify no exception
```

### Async Testing

For coroutine-based async code:
```cpp
TEST_CASE("Async payment verification", "[payment][async]") {
    drogon::app().run();  // Start event loop
    
    auto result = drogon::sync_wait(
        payment_processor->verifyPayment(request, price)
    );
    
    REQUIRE(result.status == PaymentResponse::Status::VERIFIED);
}
```

## Test Data

### Mock Data

Mock blockchain responses and test fixtures are in `tests/fixtures/`:
- `transactions.json` - Sample blockchain transactions
- `identities.json` - Sample ERC-721 identity NFTs
- `reputation.json` - Sample reputation data

### Test Configuration

Test-specific configuration in `tests/config/test.toml`:
- Uses local test blockchain (Anvil/Hardhat)
- Mock backend API endpoints
- Reduced timeouts for faster testing
- Debug logging enabled

## Continuous Integration

Tests run automatically on:
- Every commit (unit tests only)
- Pull requests (all tests including integration)
- Nightly builds (extended property-based testing)

**GitHub Actions workflow**: `.github/workflows/tests.yml`

## Debugging Tests

### Visual Studio Code

Launch configuration in `.vscode/launch.json`:
```json
{
    "name": "Debug Tests",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/bin/agentgateway_tests",
    "args": ["[payment]"],
    "cwd": "${workspaceFolder}"
}
```

### GDB

```bash
# Run tests under debugger
gdb --args ./build/bin/agentgateway_tests "[payment]"

# Set breakpoint
(gdb) break payment_processor.cpp:123
(gdb) run
```

### Logging

Enable verbose test output:
```bash
./build/bin/agentgateway_tests -s  # Show successful assertions
./build/bin/agentgateway_tests -d yes  # Show durations
```

## Performance Testing

Benchmark tests verify performance requirements:

```cpp
TEST_CASE("Payment verification latency", "[benchmark]") {
    BENCHMARK("verify payment") {
        return payment_processor->verifyPayment(request, price);
    };
}
```

Run benchmarks:
```bash
./build/bin/agentgateway_tests "[benchmark]" --benchmark-samples=100
```

## Common Issues

### Test Failures

1. **Blockchain connection timeout** - Ensure test node is running
2. **Port already in use** - Check for running AgentGateway instances
3. **Missing mock data** - Run `make test-fixtures` to regenerate
4. **Async timeout** - Increase timeout in test configuration

### Flaky Tests

If tests pass/fail intermittently:
1. Check for race conditions in async code
2. Verify proper cleanup between tests
3. Ensure deterministic test data generation
4. Add synchronization points in integration tests

## Contributing

When adding new features:
1. Write tests before implementation (TDD)
2. Ensure tests pass locally before pushing
3. Update this README if adding new test categories
4. Verify test coverage meets requirements
5. Add integration tests for user-facing features

## Resources

- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [RapidCheck Documentation](https://github.com/emil-e/rapidcheck)
- [Drogon Testing Guide](https://github.com/drogonframework/drogon/wiki/ENG-13-Testing)
- [C++ Testing Best Practices](https://google.github.io/googletest/)
