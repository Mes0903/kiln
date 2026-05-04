set(CMAKE_CXX_COMPILER clang++)
message(STATUS "Subdir CXX compiler override: ${CMAKE_CXX_COMPILER}")
add_library(subobj OBJECT subsource.cpp)
