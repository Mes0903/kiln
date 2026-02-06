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
    dmake/module_scanner.cpp
    dmake/cache_store.cpp
    dmake/genex_parser.cpp
    dmake/genex_evaluator.cpp
    dmake/compile_features.cpp
    dmake/inner/blake2b.c
    dmake/inner/sha256.cpp
    dmake/inner/md5.cpp
    dmake/builtins/message.cpp
    dmake/builtins/variable.cpp
    dmake/builtins/list.cpp
    dmake/builtins/target.cpp
    dmake/builtins/project.cpp
    dmake/builtins/file.cpp
    dmake/builtins/find_package.cpp
    dmake/builtins/find_commands.cpp
    dmake/builtins/math.cpp
    dmake/builtins/string.cpp
    dmake/builtins/process.cpp
    dmake/builtins/property.cpp
    dmake/builtins/try_compile.cpp
    dmake/builtins/path.cpp
    dmake/builtins/install.cpp
    dmake/builtins/source_properties.cpp
    dmake/install_executor.cpp
    dmake/printing.cpp
    dmake/profiler.cpp
    dmake/regex.cpp
    dmake/CMakeArray.cpp
)

target_include_directories(dmake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
target_include_directories(dmake PUBLIC ${PCRE2_INCLUDE_DIRS})
target_link_libraries(dmake PUBLIC ${PCRE2_LIBRARIES})

find_package(CURL REQUIRED)
target_link_libraries(dmake PUBLIC CURL::libcurl)

pkg_check_modules(ARCHIVE REQUIRED libarchive)
target_include_directories(dmake PUBLIC ${ARCHIVE_INCLUDE_DIRS})
target_link_libraries(dmake PUBLIC ${ARCHIVE_LIBRARIES})

find_package(CLI11 CONFIG REQUIRED)

add_executable(dmake_cli dmake-cli/main.cpp)
target_link_libraries(dmake_cli PRIVATE dmake CLI11::CLI11)

set_target_properties(dmake_cli PROPERTIES
    OUTPUT_NAME dmake
)

# Install rules
install(TARGETS dmake_cli RUNTIME DESTINATION bin)
install(TARGETS dmake LIBRARY DESTINATION lib)

if(BUILD_TESTING)
    enable_testing()

    find_package(Catch2 3 REQUIRED)
    add_executable(dmake_tests tests/main.cpp
        tests/interpreter_tests.cpp
        tests/pch_tests.cpp
        tests/command_parser.cpp
        tests/language_tests.cpp
        tests/compiler_tests.cpp
        tests/target_sources_tests.cpp
        tests/shadow_map_tests.cpp
        tests/genex_tests.cpp
        tests/compile_features_tests.cpp
        tests/source_properties_tests.cpp
        tests/cmake_array_tests.cpp)
    target_link_libraries(dmake_tests PRIVATE dmake Catch2::Catch2WithMain)

    add_test(NAME interpreter_tests COMMAND dmake_tests)
endif()
