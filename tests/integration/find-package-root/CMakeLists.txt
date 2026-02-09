project(FindPackageRootTest)

# Test 1: find_package using <PackageName>_ROOT variable
message(STATUS "Test 1: find_package with <PackageName>_ROOT")
set(MockRoot_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pkg")
find_package(MockRoot)

if(NOT MockRoot_FOUND)
    message(FATAL_ERROR "MockRoot not found via MockRoot_ROOT!")
endif()
message(STATUS "Found MockRoot ${MockRoot_VERSION} via <PackageName>_ROOT")

# Test 2: NO_PACKAGE_ROOT_PATH should skip root paths
unset(MockRoot_FOUND)
unset(MockRoot_DIR)
message(STATUS "Test 2: find_package with NO_PACKAGE_ROOT_PATH (should NOT find)")
find_package(MockRoot NO_PACKAGE_ROOT_PATH)

if(MockRoot_FOUND)
    message(FATAL_ERROR "MockRoot should NOT be found with NO_PACKAGE_ROOT_PATH!")
endif()
message(STATUS "Correctly not found with NO_PACKAGE_ROOT_PATH")

# Test 3: CMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE should skip root paths
unset(MockRoot_FOUND)
unset(MockRoot_DIR)
message(STATUS "Test 3: find_package with CMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE")
set(CMAKE_FIND_USE_PACKAGE_ROOT_PATH FALSE)
find_package(MockRoot)

if(MockRoot_FOUND)
    message(FATAL_ERROR "MockRoot should NOT be found with CMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE!")
endif()
message(STATUS "Correctly not found with CMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE")

# Test 4: Re-enable and verify it works again
unset(MockRoot_FOUND)
unset(MockRoot_DIR)
unset(CMAKE_FIND_USE_PACKAGE_ROOT_PATH)
message(STATUS "Test 4: find_package after re-enabling root paths")
find_package(MockRoot)

if(NOT MockRoot_FOUND)
    message(FATAL_ERROR "MockRoot not found after re-enabling root paths!")
endif()
message(STATUS "Found MockRoot ${MockRoot_VERSION} after re-enabling")

# Test 5: find_path/find_library/find_program respect <PackageName>_ROOT
# via CMAKE_FIND_PACKAGE_NAME (propagated through Find modules)
message(STATUS "Test 5: find_* commands respect <PackageName>_ROOT via Find module")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")
set(MockModule_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pkg")
find_package(MockModule)

if(NOT MockModule_FOUND)
    message(FATAL_ERROR "MockModule not found - find_path did not use MockModule_ROOT!")
endif()
message(STATUS "find_path correctly used <PackageName>_ROOT inside Find module")

# Test 6: NO_PACKAGE_ROOT_PATH on find_path directly
set(MOCKMODULE_INCLUDE_DIR "MOCKMODULE_INCLUDE_DIR-NOTFOUND")
set(CMAKE_FIND_PACKAGE_NAME "MockModule")
find_path(MOCKMODULE_INCLUDE_DIR NAMES mockroot.h NO_PACKAGE_ROOT_PATH)
unset(CMAKE_FIND_PACKAGE_NAME)

if(MOCKMODULE_INCLUDE_DIR AND NOT MOCKMODULE_INCLUDE_DIR MATCHES "NOTFOUND")
    message(FATAL_ERROR "find_path should NOT find mockroot.h with NO_PACKAGE_ROOT_PATH!")
endif()
message(STATUS "find_path correctly skipped root paths with NO_PACKAGE_ROOT_PATH")

# Build dummy executable
add_executable(main main.cpp)
