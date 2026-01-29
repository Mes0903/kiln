cmake_minimum_required(VERSION 3.15)
project(custom_command_test)

# Test 1: OUTPUT form - generate a source file
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    COMMAND ${CMAKE_COMMAND} -E echo "int get_value() { return 42; }" > ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    COMMENT "Generating source file"
)

# Test 2: OUTPUT form with DEPENDS
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/config.h
    COMMAND ${CMAKE_COMMAND} -E echo "#define CONFIG_VALUE 123" > ${CMAKE_CURRENT_BINARY_DIR}/config.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/input.txt
    COMMENT "Generating config header"
)

# Executable that uses generated sources
add_executable(myapp
    main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
)
target_include_directories(myapp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Test 3: TARGET form - POST_BUILD
add_custom_command(TARGET myapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete!" > ${CMAKE_CURRENT_BINARY_DIR}/build_marker.txt
    COMMENT "Post-build marker"
)

# Test 4: TARGET form - PRE_BUILD (note: on non-VS generators this runs before dependencies, like PRE_LINK)
add_custom_command(TARGET myapp PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Starting build..." > ${CMAKE_CURRENT_BINARY_DIR}/pre_marker.txt
    COMMENT "Pre-build marker"
)
