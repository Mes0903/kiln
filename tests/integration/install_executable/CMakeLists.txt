cmake_minimum_required(VERSION 3.15)
project(install_executable_test)

add_executable(myapp main.cpp)

install(TARGETS myapp RUNTIME DESTINATION bin)
