cmake_minimum_required(VERSION 3.10)
project(DirectoryPropertiesTest)

# Test 1: Retroactive application - target created BEFORE add_definitions
add_executable(before before.cpp)

# Add definitions after target creation (should be applied retroactively)
add_definitions(-DTEST_VALUE=100)

# Test 2: add_compile_definitions (modern style, no -D prefix)
add_compile_definitions(MODERN_DEF=200)

# Test 3: add_compile_options
add_compile_options(-DCOMPILE_OPTION=300)

# Test 4: Normal application - target created AFTER directory properties
add_executable(after after.cpp)

# Test 5: Create a library for link_libraries test
# Put it in a subdirectory to avoid retroactive self-linking
add_subdirectory(libdir)

# Test 6: link_libraries (should link to subsequent targets)
link_libraries(mylib)

# Test 7: Target created after link_libraries
add_executable(linked linked.cpp)

# Test 8: Subdirectory inheritance
add_subdirectory(sub)
