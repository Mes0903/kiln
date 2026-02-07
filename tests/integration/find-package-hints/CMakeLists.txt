project(FindPackageHintsTest)

# Test 1: find_package with HINTS pointing to config directory
message(STATUS "Test 1: find_package with HINTS")
find_package(MockHints HINTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pkg")

if(NOT MockHints_FOUND)
    message(FATAL_ERROR "MockHints not found via HINTS!")
endif()
message(STATUS "Found MockHints ${MockHints_VERSION} via HINTS")

# Test 2: find_package with PATHS
unset(MockHints_FOUND)
unset(MockHints_DIR)
message(STATUS "Test 2: find_package with PATHS")
find_package(MockHints PATHS "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pkg")

if(NOT MockHints_FOUND)
    message(FATAL_ERROR "MockHints not found via PATHS!")
endif()
message(STATUS "Found MockHints ${MockHints_VERSION} via PATHS")

# Test 3: find_package with NO_DEFAULT_PATH + HINTS (should still find via HINTS)
unset(MockHints_FOUND)
unset(MockHints_DIR)
message(STATUS "Test 3: find_package with NO_DEFAULT_PATH + HINTS")
find_package(MockHints NO_DEFAULT_PATH HINTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pkg")

if(NOT MockHints_FOUND)
    message(FATAL_ERROR "MockHints not found via NO_DEFAULT_PATH + HINTS!")
endif()
message(STATUS "Found MockHints ${MockHints_VERSION} via NO_DEFAULT_PATH + HINTS")

# Build dummy executable
add_executable(main main.cpp)
