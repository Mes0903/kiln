cmake_minimum_required(VERSION 3.15)
project(system_includes_test)

# Create an interface library with SYSTEM includes
add_library(syslib INTERFACE)
target_include_directories(syslib SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sysinclude)

# Create an interface library with normal includes
add_library(normallib INTERFACE)
target_include_directories(normallib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Create executable that links to both
add_executable(app main.cpp)
target_link_libraries(app PRIVATE syslib normallib)

# Test BEFORE flag - prepends instead of appends
add_executable(before_test before_test.cpp)
target_include_directories(before_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/first)
target_include_directories(before_test BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/second)
