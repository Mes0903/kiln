cmake_minimum_required(VERSION 3.28)
project(modules_import_std CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(import_std_app main.cpp)
