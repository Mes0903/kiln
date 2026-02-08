project(test CXX)

set(VERSION "v1.2.3")

add_executable(test_exe test_main.cpp)
target_compile_definitions(test_exe PRIVATE
  MY_VERSION="${VERSION}"
)
