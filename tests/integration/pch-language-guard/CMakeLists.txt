cmake_minimum_required(VERSION 3.16)
project(pch_language_guard C CXX)

# Mixed C/C++ library with PCH guarded by COMPILE_LANGUAGE genex.
# C sources must not get the C++ PCH -include flag.
add_library(mylib SHARED
    hello.cpp
    util.c
)

set_property(TARGET mylib APPEND PROPERTY PRECOMPILE_HEADERS
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>")

# Executable with SKIP_PRECOMPILE_HEADERS on a source that defines a macro
# before including headers (like Qt's removed_api.cpp pattern).
add_executable(myapp
    main.cpp
    removed_api.cpp
)

target_precompile_headers(myapp PRIVATE pch.h)
set_source_files_properties(removed_api.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# removed_api.cpp defines BUILD_REMOVED_API before includes;
# the PCH would clobber that ordering, so it must be excluded.
target_link_libraries(myapp PRIVATE mylib)
