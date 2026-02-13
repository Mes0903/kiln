cmake_minimum_required(VERSION 3.15)
project(ExternalProjectBuildtimeTest)

include(ExternalProject)

# CMake-based ExternalProject - tasks should be injected into parent graph
ExternalProject_Add(mylib_ep
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mylib_src
    BINARY_DIR ${CMAKE_BINARY_DIR}/_ep/mylib
    INSTALL_DIR ${CMAKE_BINARY_DIR}/install
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)

# Create an imported library that wraps the EP output
# Note: We link against the build output, not install output, since install is separate
add_library(mylib STATIC IMPORTED)
set_target_properties(mylib PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/_ep/mylib/libmylib.a
)
target_include_directories(mylib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/mylib_src)
add_dependencies(mylib mylib_ep)

# Main executable that links against the imported library
add_executable(main_app main.cpp)
target_link_libraries(main_app PRIVATE mylib)

dmake_dump_target_info(main_app AT_BUILD)
