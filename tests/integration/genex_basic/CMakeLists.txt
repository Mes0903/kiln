cmake_minimum_required(VERSION 3.15)
project(genex_test)

add_library(mylib INTERFACE)

# Test BUILD_INTERFACE and INSTALL_INTERFACE
target_include_directories(mylib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Test CONFIG conditionals
target_compile_definitions(mylib INTERFACE
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:RELEASE_BUILD>
)

# Test simple CONFIG
target_compile_options(mylib INTERFACE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Release>:-O3>
)

add_executable(test_exe main.cpp)
target_link_libraries(test_exe PRIVATE mylib)
