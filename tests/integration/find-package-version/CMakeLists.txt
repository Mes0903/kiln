project(FindPackageVersionTest)

# Add our mock packages to the search path
set(CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mock_packages/MockPkg)

# Test 1: Compatible version (Request 2.0) -> Should find 2.0.0
message(STATUS "Test 1: Finding MockPkg 2.0 (Exact-ish)...")
find_package(MockPkg 2.0 REQUIRED)
if(MockPkg_FOUND)
    message(STATUS "Found MockPkg 2.0 as expected.")
else()
    message(FATAL_ERROR "Failed to find MockPkg 2.0")
endif()

# Test 2: Incompatible version (Request 3.0) -> Should NOT find 2.0.0
# Note: Since dmake currently ignores version, this is expected to FAIL the test once we fix it
# But initially it will PASS finding the package (and we assert that it shouldn't have)
message(STATUS "Test 2: Finding MockPkg 3.0 (Incompatible)...")
find_package(MockPkg 3.0 QUIET)
if(MockPkg_FOUND)
    # If version checking is ignored, it will find it.
    # We want to assert that MockPkg_FOUND is FALSE for this case.
    # However, find_package doesn't clear the variable if it was already set by previous call unless we clear it or use different name?
    # CMake find_package does not clear the _FOUND variable if already set and same args?
    # Actually, standard find_package might re-run. Let's unset to be sure.
endif()

unset(MockPkg_FOUND)
unset(MockPkg_DIR)
find_package(MockPkg 3.0 QUIET)

if(MockPkg_FOUND)
    message(SEND_ERROR "Incorrectly found MockPkg 2.0.0 when 3.0 was requested!")
else()
    message(STATUS "Correctly did not find MockPkg 3.0")
endif()

# Test 3: Compatible version (Request 2.0.0)
unset(MockPkg_FOUND)
unset(MockPkg_DIR)
find_package(MockPkg 2.0.0 REQUIRED)
message(STATUS "Found MockPkg 2.0.0")

# Create dummy target
add_executable(main main.cpp)
