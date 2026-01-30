cmake_minimum_required(VERSION 3.15)
project(CompileFeaturesTest)

# Test 1: Using cxx_std_17 should set C++17
add_executable(test_std17 main.cpp)
target_compile_features(test_std17 PRIVATE cxx_std_17)

# Test 2: Using specific C++11 feature
add_executable(test_lambdas test_lambdas.cpp)
target_compile_features(test_lambdas PRIVATE cxx_lambdas)

# Test 3: Using C++14 feature should set C++14
add_executable(test_std14 test_generic_lambda.cpp)
target_compile_features(test_std14 PRIVATE cxx_generic_lambdas)

# Test 4: PUBLIC features should propagate
add_library(lib_with_features STATIC lib.cpp)
target_compile_features(lib_with_features PUBLIC cxx_std_20)

add_executable(test_propagation use_lib.cpp)
target_link_libraries(test_propagation PRIVATE lib_with_features)

# Test 5: Override lower standard with feature requirement
# Set standard to 11, but require C++17 feature
add_executable(test_override override.cpp)
set_property(TARGET test_override PROPERTY CXX_STANDARD 11)
target_compile_features(test_override PRIVATE cxx_std_17)

# Test 6: C language features
add_executable(test_c test_c.c)
target_compile_features(test_c PRIVATE c_std_99 c_variadic_macros)
