# FindRapidCheck.cmake - Find or build RapidCheck property-based testing library
#
# This module defines:
#   RapidCheck_FOUND - System has RapidCheck
#   RapidCheck_INCLUDE_DIRS - The RapidCheck include directories
#   RapidCheck_LIBRARIES - The libraries needed to use RapidCheck
#   RapidCheck::RapidCheck - Imported target for RapidCheck

include(FetchContent)

# Try to find RapidCheck installed on the system
find_path(RapidCheck_INCLUDE_DIR
    NAMES rapidcheck.h
    PATH_SUFFIXES rapidcheck
    DOC "RapidCheck include directory"
)

find_library(RapidCheck_LIBRARY
    NAMES rapidcheck
    DOC "RapidCheck library"
)

# If not found, download and build from source
if(NOT RapidCheck_INCLUDE_DIR OR NOT RapidCheck_LIBRARY)
    message(STATUS "RapidCheck not found, downloading from GitHub...")
    
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    
    # Disable RapidCheck tests and examples
    set(RC_ENABLE_TESTS OFF CACHE BOOL "Build RapidCheck tests" FORCE)
    set(RC_ENABLE_EXAMPLES OFF CACHE BOOL "Build RapidCheck examples" FORCE)
    set(RC_ENABLE_CATCH OFF CACHE BOOL "Build RapidCheck Catch integration" FORCE)
    set(RC_ENABLE_GMOCK OFF CACHE BOOL "Build RapidCheck GMock integration" FORCE)
    set(RC_ENABLE_BOOST_TEST OFF CACHE BOOL "Build RapidCheck Boost.Test integration" FORCE)
    
    FetchContent_MakeAvailable(rapidcheck)
    
    # Set variables for the fetched content
    set(RapidCheck_INCLUDE_DIR ${rapidcheck_SOURCE_DIR}/include)
    set(RapidCheck_LIBRARY rapidcheck)
    set(RapidCheck_FOUND TRUE)
    
    message(STATUS "RapidCheck downloaded and configured")
else()
    set(RapidCheck_FOUND TRUE)
    message(STATUS "RapidCheck found: ${RapidCheck_LIBRARY}")
endif()

# Create imported target
if(RapidCheck_FOUND AND NOT TARGET RapidCheck::RapidCheck)
    add_library(RapidCheck::RapidCheck INTERFACE IMPORTED)
    set_target_properties(RapidCheck::RapidCheck PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RapidCheck_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${RapidCheck_LIBRARY}"
    )
endif()

# Set output variables
set(RapidCheck_INCLUDE_DIRS ${RapidCheck_INCLUDE_DIR})
set(RapidCheck_LIBRARIES ${RapidCheck_LIBRARY})

# Standard package handling
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RapidCheck
    REQUIRED_VARS RapidCheck_INCLUDE_DIR RapidCheck_LIBRARY
    FAIL_MESSAGE "RapidCheck not found and could not be downloaded"
)

mark_as_advanced(RapidCheck_INCLUDE_DIR RapidCheck_LIBRARY)
