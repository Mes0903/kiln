cmake_minimum_required(VERSION 3.15)
project(kiln)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

find_package(pcre2 CONFIG QUIET)
if(TARGET PCRE2::8BIT)
  set(KILN_PCRE2_TARGET PCRE2::8BIT)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
  set(KILN_PCRE2_TARGET PkgConfig::PCRE2)
endif()

find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(pugixml REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)

# version.cpp is regenerated every build via a custom target, but only
set(KILN_VERSION_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/version.cpp")
if(NOT EXISTS "${KILN_VERSION_CPP}")
  file(WRITE "${KILN_VERSION_CPP}"
        "#include \"kiln/version.hpp\"\nnamespace kiln {\nstd::string_view version() noexcept { return \"0.0.0\"; }\nstd::string_view version_full() noexcept { return \"0.0.0\"; }\n}\n")
endif()
add_custom_target(kiln_version_gen ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cpp.in
        -DOUT_FILE=${KILN_VERSION_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${KILN_VERSION_CPP}
    COMMENT "Resolving kiln version from git"
    VERBATIM
)

set(KILN_PLATFORM_SOURCES)
if(WIN32)
  list(APPEND KILN_PLATFORM_SOURCES
    kiln/platform/env_win32.cpp
    kiln/platform/host_win32.cpp
    kiln/platform/process_win32.cpp
  )
else()
  list(APPEND KILN_PLATFORM_SOURCES
    kiln/platform/env_posix.cpp
    kiln/platform/host_posix.cpp
    kiln/platform/process_posix.cpp
  )
endif()

add_library(kiln
    kiln/cmake-language.cpp
    kiln/interperter.cpp
    kiln/target.cpp
    kiln/build_system.cpp
    kiln/utils.cpp
    kiln/command_parser.cpp
    kiln/language.cpp
    kiln/module_scanner.cpp
    kiln/cache_store.cpp
    kiln/genex_parser.cpp
    kiln/genex_evaluator.cpp
    kiln/compile_features.cpp
    kiln/inner/blake2b.c
    kiln/inner/sha256.cpp
    kiln/inner/md5.cpp
    kiln/inner/sha1.cpp
    kiln/builtins/message.cpp
    kiln/builtins/variable.cpp
    kiln/builtins/list.cpp
    kiln/builtins/target.cpp
    kiln/builtins/project.cpp
    kiln/builtins/file.cpp
    kiln/builtins/find_package.cpp
    kiln/builtins/find_commands.cpp
    kiln/builtins/math.cpp
    kiln/builtins/string.cpp
    kiln/builtins/process.cpp
    kiln/builtins/property.cpp
    kiln/builtins/try_compile.cpp
    kiln/builtins/path.cpp
    kiln/builtins/install.cpp
    kiln/builtins/export_generator.cpp
    kiln/builtins/source_properties.cpp
    kiln/builtins/system_info.cpp
    kiln/intercept/download_utils.cpp
    kiln/intercept/external_project.cpp
    kiln/intercept/external_project_target.cpp
    kiln/intercept/fetch_content.cpp
    kiln/install_executor.cpp
    kiln/install_plan.cpp
    kiln/printing.cpp
    kiln/progress_bar.cpp
    kiln/profiler.cpp
    kiln/regex.cpp
    kiln/CMakeArray.cpp
    kiln/tool_mode.cpp
    kiln/condition_evaluator.cpp
    kiln/ast_cache.cpp
    kiln/path.cpp
    kiln/debugger.cpp
    kiln/autogen.cpp
    kiln/presets.cpp
    ${KILN_PLATFORM_SOURCES}
    ${KILN_VERSION_CPP}
)
add_dependencies(kiln kiln_version_gen)

target_include_directories(kiln PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kiln PUBLIC
    ${KILN_PCRE2_TARGET}
    CURL::libcurl
    LibArchive::LibArchive
    pugixml::pugixml
    glaze::glaze
)

target_precompile_headers(kiln PRIVATE kiln/pch.hpp)

add_executable(kiln_cli
    kiln-cli/main.cpp
    kiln-cli/linenoise-ng/src/linenoise.cpp
    kiln-cli/linenoise-ng/src/ConvertUTF.cpp
    kiln-cli/linenoise-ng/src/wcwidth.cpp
)
target_include_directories(kiln_cli PRIVATE kiln-cli/linenoise-ng/include)
target_link_libraries(kiln_cli PRIVATE kiln CLI11::CLI11)
set_target_properties(kiln_cli PROPERTIES OUTPUT_NAME kiln)

include(GNUInstallDirs)

install(TARGETS kiln_cli kiln
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY kiln/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kiln
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
)

include(CTest)
if(BUILD_TESTING)
  find_package(Catch2 3 REQUIRED)
  add_executable(kiln_tests tests/main.cpp
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
        tests/cmake_array_tests.cpp
        tests/resolve_tests.cpp
        tests/path_tests.cpp
        tests/platform_tests.cpp
        tests/version_compare_tests.cpp
        tests/p1689_parse_tests.cpp)
  target_link_libraries(kiln_tests PRIVATE kiln Catch2::Catch2WithMain)

  add_test(NAME interpreter_tests COMMAND kiln_tests)
endif()
