cmake_minimum_required(VERSION 3.15)
project(AliasTest VERSION 1.2.3)

# Create a static library
add_library(mylib STATIC mylib.cpp)

# Create a namespaced alias for the library
add_library(MyNamespace::MyLib ALIAS mylib)

# Create another library
add_library(utils STATIC utils.cpp)

# Create an alias for utils
add_library(MyNamespace::Utils ALIAS utils)

# Create an executable
add_executable(myapp main.cpp)

# Create a namespaced alias for the executable
add_executable(MyNamespace::App ALIAS myapp)

# Link using aliases (should resolve correctly)
target_link_libraries(myapp PRIVATE MyNamespace::MyLib MyNamespace::Utils)

# Verify PROJECT_VERSION variables are set correctly
message(STATUS "PROJECT_VERSION: ${PROJECT_VERSION}")
if(NOT PROJECT_VERSION STREQUAL "1.2.3")
    message(FATAL_ERROR "PROJECT_VERSION should be 1.2.3, got: ${PROJECT_VERSION}")
endif()
if(NOT PROJECT_VERSION_MAJOR STREQUAL "1")
    message(FATAL_ERROR "PROJECT_VERSION_MAJOR should be 1")
endif()
if(NOT PROJECT_VERSION_MINOR STREQUAL "2")
    message(FATAL_ERROR "PROJECT_VERSION_MINOR should be 2")
endif()
if(NOT PROJECT_VERSION_PATCH STREQUAL "3")
    message(FATAL_ERROR "PROJECT_VERSION_PATCH should be 3")
endif()
if(NOT AliasTest_VERSION STREQUAL "1.2.3")
    message(FATAL_ERROR "AliasTest_VERSION should be 1.2.3")
endif()
