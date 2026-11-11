# CMake Custom Modules

This directory contains custom CMake Find modules for dependencies that are not available through vcpkg or require special handling.

## Available Modules

### FindRapidCheck.cmake

Finds or downloads the RapidCheck property-based testing library.

**Why Custom?**
RapidCheck is not available in vcpkg by default, so this module:
1. First attempts to find RapidCheck installed on the system
2. If not found, automatically downloads it from GitHub using FetchContent
3. Configures it for use with Google Test integration

**Usage in CMake:**
```cmake
find_package(RapidCheck REQUIRED)
target_link_libraries(my_test PRIVATE RapidCheck::RapidCheck GTest::gtest)
```

**Variables Set:**
- `RapidCheck_FOUND` - TRUE if RapidCheck is available
- `RapidCheck_INCLUDE_DIRS` - Include directories
- `RapidCheck_LIBRARIES` - Libraries to link against
- `RapidCheck::RapidCheck` - Imported target

## Adding New Modules

To add a new Find module:

1. Create `Find<PackageName>.cmake` in this directory
2. Follow CMake Find module conventions
3. Set appropriate variables and create imported targets
4. Document it in this README

## References

- [CMake Find Modules Documentation](https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules)
- [RapidCheck GitHub](https://github.com/emil-e/rapidcheck)
