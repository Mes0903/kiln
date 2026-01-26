project(pch)
add_executable(pch_test main.cpp)
target_precompile_headers(pch_test PRIVATE pch.hpp)
