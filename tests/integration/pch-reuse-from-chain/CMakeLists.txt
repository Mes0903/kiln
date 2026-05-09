project(pch_reuse_chain)

# Chain: tail REUSE_FROM middle REUSE_FROM head
# The PCH is owned by `head`. `middle` and `tail` should both use head's .gch.
add_executable(head main_head.cpp)
target_precompile_headers(head PRIVATE pch.hpp)

add_executable(middle main_middle.cpp)
target_precompile_headers(middle REUSE_FROM head)

add_executable(tail main_tail.cpp)
target_precompile_headers(tail REUSE_FROM middle)
