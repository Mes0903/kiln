cmake_minimum_required(VERSION 3.15)
project(AuxSourceDirectoryTest)

# Use aux_source_directory to collect sources from src/
aux_source_directory(src SOURCES)

# Verify we got the expected files
message(STATUS "Found sources: ${SOURCES}")

# Create executable with collected sources plus main.cpp
add_executable(test_aux_source ${SOURCES} main.cpp)
