cmake_minimum_required(VERSION 3.15)
project(InterfacePropertiesTest)

# Create a header-only library
add_library(mylib INTERFACE)

# Set INTERFACE properties using set_target_properties
# These should be propagated to consumers
set_target_properties(mylib PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "USE_MYLIB;MYLIB_VERSION=2"
    INTERFACE_COMPILE_OPTIONS "-DVIA_COMPILE_OPTIONS"
)

# Create an executable that uses the library
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)

# Dump target info to verify propagation (for debugging)
kiln_dump_target_info(app)
