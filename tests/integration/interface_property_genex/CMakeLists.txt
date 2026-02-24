cmake_minimum_required(VERSION 3.20)
project(InterfacePropertyGenex CXX)

# A library with INTERFACE include directories and compile definitions
add_library(provider SHARED provider.cpp)
target_include_directories(provider INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include_a")
target_compile_definitions(provider INTERFACE "FROM_PROVIDER=1")

# Consumer uses genex to pull INTERFACE_ properties directly,
# WITHOUT target_link_libraries. This is the pattern Qt uses via
# qt_internal_add_target_include_dirs().
add_library(consumer SHARED consumer.cpp)
target_include_directories(consumer PRIVATE
    "$<TARGET_PROPERTY:provider,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_definitions(consumer PRIVATE
    "$<TARGET_PROPERTY:provider,INTERFACE_COMPILE_DEFINITIONS>")

# Also test PUBLIC visibility contributing to INTERFACE_
add_library(pub_provider SHARED provider.cpp)
target_include_directories(pub_provider PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include_b")

add_library(pub_consumer SHARED pub_consumer.cpp)
target_include_directories(pub_consumer PRIVATE
    "$<TARGET_PROPERTY:pub_provider,INTERFACE_INCLUDE_DIRECTORIES>")
