cmake_minimum_required(VERSION 3.28)
project(modules_cross_target CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library that exports module Foo. Implementation unit lives next to the
# module-interface unit; consumers see Foo via the PUBLIC cxx_modules file set.
add_library(foo STATIC liba/foo_impl.cpp)
target_sources(foo
    PUBLIC
        FILE_SET cxx_modules
        TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/liba
        FILES liba/foo.cppm
)

# App imports Foo. With cross-target modules wired up, app's compile resolves
# `import Foo;` to foo's BMI via foo's module-exports manifest.
add_executable(app app/main.cpp)
target_link_libraries(app PRIVATE foo)
