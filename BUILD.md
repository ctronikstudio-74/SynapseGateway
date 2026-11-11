# AgentGateway Build Instructions

## Prerequisites

### All Platforms
- CMake 3.20 or higher
- C++20 compatible compiler
- Git (for version control)

### Windows

Choose one of the following compiler toolchains:

#### Option 1: Visual Studio 2022 (MSVC) - Recommended
- Download and install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)
- During installation, select "Desktop development with C++"
- Includes:
  - MSVC C++ compiler (cl.exe)
  - Windows SDK
  - NMake build tool
  - MSBuild

#### Option 2: MinGW-w64
- Download from [MinGW-w64](https://www.mingw-w64.org/)
- Add MinGW `bin` directory to system PATH
- Includes: g++, mingw32-make

#### Option 3: LLVM/Clang for Windows
- Download from [LLVM Releases](https://github.com/llvm/llvm-project/releases)
- Install and add to system PATH
- Includes: clang++

### Linux

#### Ubuntu/Debian
```bash
# GCC (Recommended)
sudo apt-get update
sudo apt-get install build-essential cmake g++-11

# Or Clang
sudo apt-get install clang-14 cmake
```

#### Fedora/RHEL
```bash
# GCC
sudo dnf install gcc-c++ cmake

# Or Clang
sudo dnf install clang cmake
```

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake via Homebrew
brew install cmake
```

## Build Configuration

The project uses CMake Presets for cross-platform builds. The presets automatically configure the build system for your platform and compiler.

### Available Presets

#### Windows
- `windows-msvc-debug` - MSVC Debug build
- `windows-msvc-release` - MSVC Release build (optimized)
- `windows-clang-debug` - Clang Debug build
- `windows-clang-release` - Clang Release build

#### Linux
- `linux-gcc-debug` - GCC Debug build
- `linux-gcc-release` - GCC Release build (optimized)
- `linux-clang-debug` - Clang Debug build
- `linux-clang-release` - Clang Release build

#### macOS
- `macos-clang-debug` - AppleClang Debug build
- `macos-clang-release` - AppleClang Release build (optimized)
- `macos-arm64-debug` - ARM64 (Apple Silicon) Debug build
- `macos-arm64-release` - ARM64 Release build
- `macos-universal` - Universal Binary (x86_64 + ARM64)

## Building the Project

### Using CMake Presets (Recommended)

#### Windows (MSVC)

Open **Developer Command Prompt for VS 2022** (search in Start Menu), then:

```powershell
# Configure
cmake --preset windows-msvc-release

# Build
cmake --build --preset windows-msvc-release

# Run tests (when available)
ctest --preset windows-msvc-release
```

#### Linux (GCC)

```bash
# Configure
cmake --preset linux-gcc-release

# Build
cmake --build --preset linux-gcc-release

# Run tests (when available)
ctest --preset linux-gcc-release
```

#### macOS (Clang)

```bash
# Configure
cmake --preset macos-clang-release

# Build
cmake --build --preset macos-clang-release

# Run tests (when available)
ctest --preset macos-clang-release
```

### Using CMake Without Presets

If presets don't work or you want custom configuration:

#### Windows (NMake)
```powershell
# Open Developer Command Prompt for VS 2022
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

#### Windows (MinGW)
```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

#### Linux/macOS
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)  # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS
```

## Build Outputs

- Binaries: `out/build/<preset-name>/` or `build/`
- Executable: `gateway_node` (or `gateway_node.exe` on Windows)
- Compile commands: `compile_commands.json` (for IDE/LSP integration)

## IDE Integration

### Visual Studio Code
1. Install the CMake Tools extension
2. Open the project folder
3. Select a CMake preset from the status bar
4. Build using Ctrl+Shift+B

### Visual Studio 2022
1. Open the folder containing CMakeLists.txt
2. Visual Studio will automatically detect CMake configuration
3. Select a preset from the configuration dropdown
4. Build using Ctrl+Shift+B

### CLion
1. Open the project
2. CLion will automatically load CMake configuration
3. Select build configuration from toolbar
4. Build using Ctrl+F9

## Compiler Flags

The build system automatically configures platform-specific compiler flags:

### MSVC (Windows)
- `/W4` - Warning level 4
- `/WX` - Treat warnings as errors
- `/permissive-` - Standards conformance
- `/std:c++20` - C++20 standard
- `/O2` (Release) - Optimize for speed
- `/GL /LTCG` (Release) - Link-time code generation

### GCC (Linux)
- `-Wall -Wextra -Werror` - Enable warnings
- `-std=c++20` - C++20 standard
- `-O3 -march=native -flto` (Release) - Maximum optimization
- `-pthread` - Thread support

### Clang (Linux/macOS)
- `-Wall -Wextra -Werror` - Enable warnings
- `-std=c++20` - C++20 standard
- `-O3 -flto` (Release) - Maximum optimization
- `-march=native` (Linux Release) - CPU-specific optimization

## Platform-Specific Libraries

The build system automatically links required platform libraries:

- **Windows**: `ws2_32` (Winsock2 for networking)
- **Linux/macOS**: `pthread` (POSIX threads)
- **macOS**: `CoreFoundation` (system framework)

## Troubleshooting

### "CMake Error: CMAKE_CXX_COMPILER not set"
- Ensure a C++ compiler is installed and in system PATH
- On Windows, run CMake from **Developer Command Prompt for VS**

### "No CMAKE_CXX_COMPILER could be found"
- Install a C++20 compatible compiler (see Prerequisites)
- Specify compiler explicitly:
  ```bash
  cmake -B build -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
  ```

### "Generator not found"
- Install the required build tool (NMake, Make, Ninja)
- Use a different generator with `-G` flag

### Preset not working
- Verify CMake version: `cmake --version` (must be 3.20+)
- Try building without presets (see alternative method above)

## Next Steps

After successful build:
1. Dependencies will be added in task 1.2 (Drogon, Boost, OpenSSL, etc.)
2. Unit tests will be added in task 1.2
3. Run the executable: `./out/build/<preset>/gateway_node`

## References

- [CMake Documentation](https://cmake.org/documentation/)
- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [C++20 Support](https://en.cppreference.com/w/cpp/compiler_support/20)
