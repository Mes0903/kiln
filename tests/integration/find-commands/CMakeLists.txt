project(FindCommandsTest)

# Test find_program
find_program(BASH_PATH bash REQUIRED)
message(STATUS "Found bash: ${BASH_PATH}")

# Test find_program with NAMES
find_program(SHELL_PATH NAMES bash sh REQUIRED)
message(STATUS "Found shell: ${SHELL_PATH}")

# Test find_library
find_library(MATH_LIB m REQUIRED)
message(STATUS "Found libm: ${MATH_LIB}")

# Test find_library with pthread (requires lib prefix handling)
find_library(PTHREAD_LIB pthread REQUIRED)
message(STATUS "Found pthread: ${PTHREAD_LIB}")

# Test find_file
find_file(STDIO_H stdio.h PATHS /usr/include REQUIRED)
message(STATUS "Found stdio.h: ${STDIO_H}")

# Test find_path - should return directory, not file
find_path(STDIO_DIR stdio.h REQUIRED)
message(STATUS "Found stdio.h directory: ${STDIO_DIR}")
if(STDIO_DIR MATCHES "stdio.h")
    message(FATAL_ERROR "find_path should return directory, not file path")
endif()

# Test find_path with PATH_SUFFIXES
find_path(LINUX_DIR version.h PATH_SUFFIXES linux)
if(LINUX_DIR)
    message(STATUS "Found version.h directory: ${LINUX_DIR}")
endif()

# Test find_path returns directory containing the file
find_path(STDLIB_DIR stdlib.h REQUIRED)
if(NOT STDLIB_DIR MATCHES "/include$")
    message(FATAL_ERROR "find_path should return include directory")
endif()
message(STATUS "Find path correctly returns directory: ${STDLIB_DIR}")

# Test find_program with PATHS
find_program(BASH_EXPLICIT bash PATHS /usr/bin /bin REQUIRED)
message(STATUS "Found bash with explicit path: ${BASH_EXPLICIT}")

# Test variable skip-search optimization
set(CACHED_BASH /usr/bin/bash)
find_program(CACHED_BASH nonexistent_should_be_skipped)
if(NOT CACHED_BASH STREQUAL "/usr/bin/bash")
    message(FATAL_ERROR "Variable skip-search optimization failed")
endif()
message(STATUS "Skip-search optimization works")

# Build a simple executable to verify the project builds
add_executable(find_test main.cpp)

# Link with the found math library (full path) - tests that kiln handles full paths correctly
target_link_libraries(find_test PRIVATE ${MATH_LIB})
