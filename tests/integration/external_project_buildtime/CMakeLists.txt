cmake_minimum_required(VERSION 3.15)
project(ExternalProjectBuildtimeTest)

include(ExternalProject)

# ExternalProject that uses custom commands (not cmake-based)
ExternalProject_Add(mylib_ep
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mylib_src
    BINARY_DIR ${CMAKE_BINARY_DIR}/_ep/mylib
    INSTALL_DIR ${CMAKE_BINARY_DIR}/install

    # Custom commands instead of cmake
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E make_directory <INSTALL_DIR>/lib
    BUILD_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/mylib_src/lib.txt <INSTALL_DIR>/lib/mylib.txt
    INSTALL_COMMAND ${CMAKE_COMMAND} -E touch <INSTALL_DIR>/lib/mylib_installed.txt
)

# Main executable that directly depends on the EP
add_executable(main_app main.cpp)
add_dependencies(main_app mylib_ep)
