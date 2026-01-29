cmake_minimum_required(VERSION 3.10)
project(AddDependenciesTest)

# Create a custom target that generates a header file using file(WRITE) via cmake script
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generate_header.cmake
    "file(WRITE \${OUTPUT_FILE} \"#define GENERATED_VALUE 42\\n\")"
)

add_custom_target(generate_header
    COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/generated.h -P ${CMAKE_CURRENT_BINARY_DIR}/generate_header.cmake
    COMMENT "Generating header file"
)

# Create an executable that depends on the generated header
add_executable(app main.cpp)

# Use add_dependencies to ensure generate_header runs before app builds
add_dependencies(app generate_header)

# Add the binary dir to includes so the generated header can be found
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
