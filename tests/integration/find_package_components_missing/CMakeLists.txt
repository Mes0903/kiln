cmake_minimum_required(VERSION 3.15)
project(FindPackageComponentsMissingTest)

# Set up a module path
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# This should fail because MissingComp is required but not found
find_package(PartialPkg REQUIRED COMPONENTS FoundComp MissingComp)

# This line should never be reached
message(FATAL_ERROR "Should not reach here - required component was missing!")
