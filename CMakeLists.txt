cmake_minimum_required(VERSION 3.15)
project(dmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dmake SHARED
    dmake/cmake-language.cpp
    dmake/interperter.cpp
    dmake/target.cpp
    dmake/build_system.cpp
    dmake/utils.cpp
    dmake/command_parser.cpp
    dmake/language.cpp
    dmake/inner/blake2b.c
    dmake/builtins/message.cpp
    dmake/builtins/variable.cpp
    dmake/builtins/list.cpp
    dmake/builtins/target.cpp
    dmake/builtins/project.cpp
    dmake/builtins/file.cpp
    dmake/builtins/find_package.cpp
    dmake/CMakeList.cpp
)

target_include_directories(dmake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(CLI11 CONFIG REQUIRED)

add_executable(dmake_cli dmake-cli/main.cpp)
target_link_libraries(dmake_cli PRIVATE dmake CLI11::CLI11)

set_target_properties(dmake_cli PROPERTIES
    OUTPUT_NAME dmake
)

if(BUILD_TESTING)
    enable_testing()

    find_package(Catch2 3 REQUIRED)
    add_executable(dmake_tests tests/main.cpp tests/interpreter_tests.cpp tests/pch_tests.cpp tests/command_parser.cpp tests/language_tests.cpp tests/compiler_tests.cpp)
    target_link_libraries(dmake_tests PRIVATE dmake Catch2::Catch2WithMain)

    add_test(NAME interpreter_tests COMMAND dmake_tests)
endif()
