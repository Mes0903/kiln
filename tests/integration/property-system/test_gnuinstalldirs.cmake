cmake_minimum_required(VERSION 3.15)
project(TestGNUInstallDirs)

# Test that we can include GNUInstallDirs without errors
include(GNUInstallDirs)

message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_INSTALL_BINDIR: ${CMAKE_INSTALL_BINDIR}")
message(STATUS "CMAKE_INSTALL_LIBDIR: ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "GNUInstallDirs loaded successfully")
