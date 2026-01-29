cmake_minimum_required(VERSION 3.15)
project(link_only_test)

# Create a library with PUBLIC include directories and compile definitions
add_library(mylib STATIC mylib.cpp)
target_include_directories(mylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(mylib PUBLIC MYLIB_ENABLED=1)

# Create an app that links normally - should get includes and definitions
add_executable(normal_app normal_main.cpp)
target_link_libraries(normal_app PRIVATE mylib)

# Create an app that uses LINK_ONLY - should NOT get includes and definitions
# This would fail to compile if INTERFACE properties were propagated
add_executable(link_only_app link_only_main.cpp)
target_link_libraries(link_only_app PRIVATE $<LINK_ONLY:mylib>)

# The link_only_app must define its own include path and definition
target_include_directories(link_only_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Note: We intentionally don't define MYLIB_ENABLED - the code handles it being undefined
