project(pch_reuse_no_pch)

add_executable(provider main.cpp)
# provider never gets target_precompile_headers

add_executable(reuser main.cpp)
target_precompile_headers(reuser REUSE_FROM provider)
