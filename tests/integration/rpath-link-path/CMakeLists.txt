cmake_minimum_required(VERSION 3.14)
project(rpath_link_path CXX)

# Link mylib against a prebuilt shared library in a non-standard location
# (vendor/libvendor.so, created by test.sh before kiln runs). kiln must
# embed that directory in mylib's RUNPATH so the dynamic linker — and any
# downstream link — can resolve libvendor.so.
add_library(mylib SHARED mylib.cpp)
target_link_libraries(mylib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libvendor.so)

# Same library, but with SKIP_BUILD_RPATH set: kiln must NOT embed the
# vendor directory.
add_library(mylib_norpath SHARED mylib.cpp)
target_link_libraries(mylib_norpath PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libvendor.so)
set_target_properties(mylib_norpath PROPERTIES SKIP_BUILD_RPATH TRUE)

add_executable(app app.cpp)
target_link_libraries(app PRIVATE mylib)
