project(CMakeLanguageTest)

# Test EVAL CODE
message(STATUS "Testing cmake_language(EVAL CODE)...")
# Note: We escape \${} so it is not expanded by the outer parser
cmake_language(EVAL CODE "
    this_does_not_exist()
")
