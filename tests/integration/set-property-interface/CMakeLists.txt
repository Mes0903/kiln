cmake_minimum_required(VERSION 3.15)
project(SetPropertyInterfaceTest)

# Create a header-only library
add_library(mylib INTERFACE IMPORTED)

# Use set_property (like FindPkgConfig.cmake does) to set INTERFACE properties
set_property(TARGET mylib PROPERTY
             INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")

set_property(TARGET mylib PROPERTY
             INTERFACE_COMPILE_DEFINITIONS "USE_MYLIB" "MYLIB_VERSION=3")

set_property(TARGET mylib PROPERTY
             INTERFACE_COMPILE_OPTIONS "-DVIA_SET_PROPERTY")

# Create an executable that uses the library
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)

# Dump target info to verify propagation
kiln_dump_target_info(app)
