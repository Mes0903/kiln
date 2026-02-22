project(pch_reuse_order)

# Reuser declared first, REUSE_FROM before provider gets its PCH
add_executable(reuser main_reuser.cpp)
target_precompile_headers(reuser REUSE_FROM provider)

add_executable(provider main_provider.cpp)
target_precompile_headers(provider PRIVATE pch.hpp)
