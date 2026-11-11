#!/bin/bash
# setup-build.sh - AgentGateway Build Setup Script for Linux/macOS
# This script checks dependencies and configures the build environment

set -e

BUILD_TYPE="${1:-Release}"
SKIP_VCPKG_CHECK="${2:-false}"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}========================================"
echo "AgentGateway Build Setup"
echo -e "========================================${NC}"

# Detect platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="linux"
    echo -e "\nPlatform: Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="macos"
    echo -e "\nPlatform: macOS"
else
    echo -e "${RED}ERROR: Unsupported platform: $OSTYPE${NC}"
    exit 1
fi

# Check for CMake
echo -e "\n${YELLOW}Checking for CMake...${NC}"
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}ERROR: CMake not found. Please install CMake 3.20 or later.${NC}"
    exit 1
fi
CMAKE_VERSION=$(cmake --version | head -n1 | grep -oP '\d+\.\d+\.\d+')
echo -e "${GREEN}  Found CMake $CMAKE_VERSION${NC}"

# Check for vcpkg
if [[ "$SKIP_VCPKG_CHECK" != "true" ]]; then
    echo -e "\n${YELLOW}Checking for vcpkg...${NC}"
    if [[ -z "$VCPKG_ROOT" ]]; then
        echo -e "${YELLOW}WARNING: VCPKG_ROOT environment variable not set.${NC}"
        echo "Please install vcpkg and set VCPKG_ROOT."
        echo ""
        echo -e "${CYAN}To install vcpkg:${NC}"
        echo "  git clone https://github.com/Microsoft/vcpkg.git"
        echo "  cd vcpkg"
        echo "  ./bootstrap-vcpkg.sh"
        echo "  export VCPKG_ROOT=\$(pwd)"
        exit 1
    fi
    echo -e "${GREEN}  Found vcpkg at: $VCPKG_ROOT${NC}"
else
    echo -e "\n${YELLOW}Skipping vcpkg check (manual dependency management)${NC}"
fi

# Check for compiler
echo -e "\n${YELLOW}Checking for C++ compiler...${NC}"
if [[ "$PLATFORM" == "linux" ]]; then
    # Check for GCC or Clang
    if command -v g++ &> /dev/null; then
        COMPILER="gcc"
        GCC_VERSION=$(g++ --version | head -n1 | grep -oP '\d+\.\d+\.\d+' | head -n1)
        echo -e "${GREEN}  Found GCC $GCC_VERSION${NC}"
    elif command -v clang++ &> /dev/null; then
        COMPILER="clang"
        CLANG_VERSION=$(clang++ --version | head -n1 | grep -oP '\d+\.\d+\.\d+' | head -n1)
        echo -e "${GREEN}  Found Clang $CLANG_VERSION${NC}"
    else
        echo -e "${RED}ERROR: No C++ compiler found. Please install GCC or Clang.${NC}"
        exit 1
    fi
elif [[ "$PLATFORM" == "macos" ]]; then
    if command -v clang++ &> /dev/null; then
        COMPILER="clang"
        CLANG_VERSION=$(clang++ --version | head -n1 | grep -oP '\d+\.\d+\.\d+' | head -n1)
        echo -e "${GREEN}  Found AppleClang $CLANG_VERSION${NC}"
    else
        echo -e "${RED}ERROR: Clang not found. Please install Xcode Command Line Tools.${NC}"
        exit 1
    fi
fi

# Check for Ninja (optional but recommended)
echo -e "\n${YELLOW}Checking for Ninja build system...${NC}"
if ! command -v ninja &> /dev/null; then
    echo -e "${YELLOW}  Ninja not found (optional, will use default generator)${NC}"
else
    echo -e "${GREEN}  Found Ninja${NC}"
fi

# Determine preset
if [[ "$PLATFORM" == "linux" ]]; then
    PRESET="${PLATFORM}-${COMPILER}-$(echo $BUILD_TYPE | tr '[:upper:]' '[:lower:]')"
else
    PRESET="${PLATFORM}-clang-$(echo $BUILD_TYPE | tr '[:upper:]' '[:lower:]')"
fi

echo -e "\n${CYAN}========================================"
echo "Configuring build with preset: $PRESET"
echo -e "========================================${NC}"

# Configure
echo -e "\n${YELLOW}Running CMake configure...${NC}"
cmake --preset "$PRESET"

echo -e "\n${GREEN}========================================"
echo "Build environment configured successfully!"
echo -e "========================================${NC}"

echo -e "\n${CYAN}To build the project, run:${NC}"
echo "  cmake --build --preset $PRESET"

echo -e "\n${CYAN}To run tests, run:${NC}"
echo "  ctest --preset $PRESET"
