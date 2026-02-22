project(pch_reuse)

add_executable(provider main_provider.cpp)
target_precompile_headers(provider PRIVATE pch.hpp)

add_executable(reuser main_reuser.cpp)
target_precompile_headers(reuser REUSE_FROM provider)
