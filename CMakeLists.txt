# CMakeLists.txt - AgentGateway C++20 Middleware Build System
# Supports cross-platform compilation: Windows (MSVC), Linux (GCC/Clang), macOS (Clang)

cmake_minimum_required(VERSION 3.20)
project(AgentGateway VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# C++20 Standard Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE/LSP support (clangd, Cursor)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Type Configuration
# ============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ============================================================================
# Platform Detection
# ============================================================================
if(WIN32)
    message(STATUS "Platform: Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    message(STATUS "Platform: macOS")
    set(PLATFORM_MACOS TRUE)
elseif(UNIX)
    message(STATUS "Platform: Linux/Unix")
    set(PLATFORM_LINUX TRUE)
endif()

# ============================================================================
# Compiler-Specific Flags
# ============================================================================

# MSVC (Windows)
if(MSVC)
    add_compile_options(
        /W4                    # Warning level 4
        /WX                    # Treat warnings as errors
        /permissive-           # Standards conformance mode
        /Zc:__cplusplus        # Enable __cplusplus macro
        /Zc:preprocessor       # Enable conforming preprocessor
        /utf-8                 # Source and execution charset UTF-8
        /EHsc                  # Exception handling model
        /MP                    # Multi-processor compilation
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
    )
    add_compile_definitions(
        _WIN32_WINNT=0x0A00    # Windows 10
        NOMINMAX               # Disable min/max macros
        WIN32_LEAN_AND_MEAN    # Exclude rarely-used Windows headers
    )
endif()

# GCC (Linux)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        -Wall                  # Enable most warnings
        -Wextra                # Extra warnings
        -Werror                # Treat warnings as errors
        -Wpedantic             # Pedantic warnings
        -Wconversion           # Warn on implicit conversions
        -Wsign-conversion      # Warn on sign conversions
        -Wno-unused-parameter  # Allow unused parameters
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-ggdb3>
    )
    add_link_options(
        $<$<CONFIG:Release>:-flto>   # Link-time optimization
    )
endif()

# Clang (Linux/macOS)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(
        -Wall                  # Enable most warnings
        -Wextra                # Extra warnings
        -Werror                # Treat warnings as errors
        -Wpedantic             # Pedantic warnings
        -Wconversion           # Warn on implicit conversions
        -Wsign-conversion      # Warn on sign conversions
        -Wno-unused-parameter  # Allow unused parameters
        -Wno-c++98-compat      # Disable C++98 compatibility warnings
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-glldb>
    )
    if(NOT APPLE)
        add_compile_options($<$<CONFIG:Release>:-march=native>)
    endif()
    add_link_options(
        $<$<CONFIG:Release>:-flto>   # Link-time optimization
    )
endif()

# ============================================================================
# vcpkg Integration
# ============================================================================
# Automatically find vcpkg toolchain if VCPKG_ROOT is set
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Add custom CMake module path for FindRapidCheck
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================================
# Third-Party Dependency Discovery
# ============================================================================
message(STATUS "Finding third-party dependencies...")

# Drogon HTTP framework (with SSL and ORM support)
find_package(Drogon CONFIG REQUIRED)
message(STATUS "  Drogon: ${Drogon_VERSION}")

# Boost libraries (Asio, Beast, System, Thread, Filesystem, JSON)
find_package(Boost 1.75 REQUIRED COMPONENTS system thread filesystem json)
message(STATUS "  Boost: ${Boost_VERSION}")

# OpenSSL for cryptography
find_package(OpenSSL 3.0 REQUIRED)
message(STATUS "  OpenSSL: ${OPENSSL_VERSION}")

# toml++ for TOML configuration parsing (header-only)
find_package(tomlplusplus CONFIG REQUIRED)
message(STATUS "  toml++: found")

# spdlog for logging
find_package(spdlog CONFIG REQUIRED)
message(STATUS "  spdlog: ${spdlog_VERSION}")

# nlohmann_json for JSON processing
find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "  nlohmann_json: ${nlohmann_json_VERSION}")

# prometheus-cpp for metrics
find_package(prometheus-cpp CONFIG REQUIRED)
message(STATUS "  prometheus-cpp: found")

# Google Test for unit testing
find_package(GTest CONFIG REQUIRED)
message(STATUS "  GTest: ${GTest_VERSION}")

message(STATUS "All dependencies found successfully!")

# ============================================================================
# Include Directories
# ============================================================================
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============================================================================
# Platform-Specific Library Detection
# ============================================================================

# Windows: ws2_32 (Winsock2) for networking
if(WIN32)
    set(PLATFORM_LIBRARIES ws2_32)
    message(STATUS "Windows libraries: ws2_32")
endif()

# Unix-like: pthread for threading
if(UNIX)
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
    message(STATUS "Unix libraries: pthread")
endif()

# macOS: Additional frameworks if needed
if(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    if(CORE_FOUNDATION)
        list(APPEND PLATFORM_LIBRARIES ${CORE_FOUNDATION})
    endif()
endif()

# ============================================================================
# Core Library
# ============================================================================
find_package(Threads REQUIRED)

add_library(gateway_core STATIC
    src/core/Configuration.cpp
    src/core/ConfigurationManager.cpp
    src/core/PaymentProcessor.cpp
    src/core/IdentityVerifier.cpp
    src/core/ReputationChecker.cpp
    src/utils/bloom_filter.cpp
    src/utils/rate_limiter.cpp
    src/blockchain/hex.cpp
    src/blockchain/keccak.cpp
    src/blockchain/create2.cpp
    src/blockchain/abi_decoder.cpp
    src/blockchain/network_selection.cpp
    src/blockchain/http_rpc_client.cpp
    src/blockchain/BlockchainConnector.cpp
    src/blockchain/network_monitor.cpp
    src/monitoring/MetricsCollector.cpp
    src/core/payment_state.cpp
    src/core/header_policy.cpp
    src/core/backend_pool.cpp
    src/core/GatewayRouter.cpp
    src/http/HttpServer.cpp
)

target_compile_features(gateway_core PUBLIC cxx_std_20)
target_include_directories(gateway_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(gateway_core PUBLIC
    tomlplusplus::tomlplusplus
    Threads::Threads
    OpenSSL::Crypto
    OpenSSL::SSL
    nlohmann_json::nlohmann_json
    Boost::system
    spdlog::spdlog
    prometheus-cpp::pull
    Drogon::Drogon
)
if(WIN32)
    target_link_libraries(gateway_core PUBLIC ws2_32 crypt32)
endif()

# ============================================================================
# Main Executable Target
# ============================================================================
add_executable(gateway_node src/main.cpp)

target_compile_features(gateway_node PUBLIC cxx_std_20)
target_link_libraries(gateway_node PRIVATE
    gateway_core

    # HTTP framework
    Drogon::Drogon

    # Boost libraries
    Boost::system
    Boost::thread
    Boost::filesystem
    Boost::json

    # Cryptography
    OpenSSL::SSL
    OpenSSL::Crypto

    # Logging
    spdlog::spdlog

    # JSON processing
    nlohmann_json::nlohmann_json

    # Metrics
    prometheus-cpp::pull

    # Platform-specific libraries
    ${PLATFORM_LIBRARIES}
)

# ============================================================================
# Installation Rules
# ============================================================================
install(TARGETS gateway_node
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install headers
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# ============================================================================
# Testing Support
# ============================================================================
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    
    message(STATUS "Testing enabled")
    message(STATUS "  Google Test: ${GTest_VERSION}")
    message(STATUS "  Property tests: in-tree 100-iteration runner")

    function(gateway_add_test name)
        add_executable(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE gateway_core GTest::gtest GTest::gtest_main)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    gateway_add_test(bloom_filter_test tests/core/bloom_filter_test.cpp)
    gateway_add_test(configuration_test tests/core/ConfigurationTest.cpp)
    gateway_add_test(cache_test tests/core/cache_test.cpp)
    gateway_add_test(rate_limiter_test tests/core/rate_limiter_test.cpp)
    gateway_add_test(models_test tests/blockchain/models_test.cpp)
    gateway_add_test(configuration_parser_test tests/core/configuration_parser_test.cpp)
    gateway_add_test(utility_properties_test tests/properties/utility_properties_test.cpp)
    gateway_add_test(configuration_roundtrip_test tests/core/configuration_roundtrip_test.cpp)
    gateway_add_test(create2_test tests/blockchain/create2_test.cpp)
    gateway_add_test(abi_decoder_test tests/blockchain/abi_decoder_test.cpp)
    gateway_add_test(connector_test tests/blockchain/connector_test.cpp)
    gateway_add_test(payment_processor_test tests/core/payment_processor_test.cpp)
    gateway_add_test(identity_reputation_test tests/core/identity_reputation_test.cpp)
    gateway_add_test(monitoring_test tests/monitoring/monitoring_test.cpp)
    gateway_add_test(header_policy_test tests/core/header_policy_test.cpp)
    gateway_add_test(payment_state_test tests/core/payment_state_test.cpp)
    gateway_add_test(gateway_router_test tests/core/gateway_router_test.cpp)
    gateway_add_test(backend_pool_test tests/core/backend_pool_test.cpp)
    gateway_add_test(gateway_flow_test tests/integration/gateway_flow_test.cpp)
    gateway_add_test(performance_test tests/perf/performance_test.cpp)
    target_compile_definitions(configuration_parser_test PRIVATE
        GATEWAY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(configuration_roundtrip_test PRIVATE
        GATEWAY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# ============================================================================
# Build Summary
# ============================================================================
message(STATUS "========================================")
message(STATUS "AgentGateway Build Configuration")
message(STATUS "========================================")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:     ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Platform:     ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "========================================")