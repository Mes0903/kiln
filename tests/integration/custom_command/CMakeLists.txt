cmake_minimum_required(VERSION 3.15)
project(custom_command_test)

# Test 1: OUTPUT form - generate a source file using a generator script
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/generated.cpp -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_source.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating source file"
)

# Test 2: OUTPUT form with DEPENDS - generate config header
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/config.h
    COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/config.h -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_config.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/input.txt
    COMMENT "Generating config header"
)

# Executable that uses generated sources
add_executable(myapp
    main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)
target_include_directories(myapp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Test 3: TARGET form - POST_BUILD
add_custom_command(TARGET myapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/build_marker.txt
    COMMENT "Post-build marker"
)

# Test 4: TARGET form - PRE_BUILD
add_custom_command(TARGET myapp PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/pre_marker.txt
    COMMENT "Pre-build marker"
)
