cmake_minimum_required(VERSION 3.15)
project(export_test)

# Create a simple library
add_library(mylib STATIC mylib.cpp)
target_include_directories(mylib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(mylib PUBLIC MYLIB_ENABLED=1)

# Create an interface library
add_library(myinterface INTERFACE)
target_compile_definitions(myinterface INTERFACE USE_INTERFACE=1)

# Create an executable that links to the library
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE mylib)

# Test export(TARGETS) - build-tree export
export(TARGETS mylib myinterface
    FILE "${CMAKE_CURRENT_BINARY_DIR}/MyLibTargets.cmake"
    NAMESPACE MyLib::
)

# Test install(TARGETS ... EXPORT) and install(EXPORT)
install(TARGETS mylib myinterface EXPORT MyLibExport
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT MyLibExport
    FILE MyLibTargets-install.cmake
    NAMESPACE MyLib::
    DESTINATION lib/cmake/MyLib
)
