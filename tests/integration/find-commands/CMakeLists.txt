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

# Link with the found math library (full path) - tests that dmake handles full paths correctly
target_link_libraries(find_test PRIVATE ${MATH_LIB})
