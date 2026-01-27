project(FindModuleTest)

# Add our local cmake directory to module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

message(STATUS "Testing find_package(MockModule)...")
find_package(MockModule REQUIRED)

if(NOT MockModule_FOUND)
    message(FATAL_ERROR "MockModule not found!")
endif()

message(STATUS "Found MockModule: ${MockModule_VERSION}")

# Build dummy executable
add_executable(main main.cpp)
