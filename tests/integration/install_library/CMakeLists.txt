cmake_minimum_required(VERSION 3.15)
project(install_library_test)

add_library(mylib SHARED lib.cpp)

set_target_properties(mylib PROPERTIES
    VERSION 1.2.3
    SOVERSION 1
)

install(TARGETS mylib LIBRARY DESTINATION lib)
