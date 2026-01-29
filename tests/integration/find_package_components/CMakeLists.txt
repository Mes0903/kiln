cmake_minimum_required(VERSION 3.15)
project(FindPackageComponentsTest)

# Set up a module path
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Test 1: Find package with all required components found
find_package(TestPkg REQUIRED COMPONENTS CompA CompB)

if(NOT TestPkg_FOUND)
    message(FATAL_ERROR "TestPkg should have been found")
endif()

if(NOT TestPkg_CompA_FOUND)
    message(FATAL_ERROR "TestPkg_CompA_FOUND should be set")
endif()

if(NOT TestPkg_CompB_FOUND)
    message(FATAL_ERROR "TestPkg_CompB_FOUND should be set")
endif()

message(STATUS "Test 1 passed: All required components found")

# Test 2: Find package with optional components
find_package(TestPkg2 COMPONENTS CompX OPTIONAL_COMPONENTS CompY CompZ)

if(NOT TestPkg2_FOUND)
    message(FATAL_ERROR "TestPkg2 should have been found")
endif()

if(NOT TestPkg2_CompX_FOUND)
    message(FATAL_ERROR "TestPkg2_CompX_FOUND should be set")
endif()

# CompY is optional and found
if(NOT TestPkg2_CompY_FOUND)
    message(STATUS "CompY not found (optional)")
endif()

# CompZ is optional and not found
if(TestPkg2_CompZ_FOUND)
    message(FATAL_ERROR "TestPkg2_CompZ_FOUND should not be set")
endif()

message(STATUS "Test 2 passed: Optional components handled correctly")

# Test 3: Test that the find module received correct variables
find_package(TestPkg3 REQUIRED COMPONENTS Alpha Beta)

# The Find module should have received these variables
if(NOT TestPkg3_FIND_COMPONENTS STREQUAL "Alpha;Beta")
    message(FATAL_ERROR "TestPkg3_FIND_COMPONENTS incorrect: '${TestPkg3_FIND_COMPONENTS}'")
endif()

if(NOT TestPkg3_FIND_REQUIRED_Alpha)
    message(FATAL_ERROR "TestPkg3_FIND_REQUIRED_Alpha should be TRUE")
endif()

if(NOT TestPkg3_FIND_REQUIRED_Beta)
    message(FATAL_ERROR "TestPkg3_FIND_REQUIRED_Beta should be TRUE")
endif()

message(STATUS "Test 3 passed: Component variables set correctly")

message(STATUS "All tests passed!")
