cmake_minimum_required(VERSION 3.15)
project(ReturnTest)

message("=== Test 1: return() from function ===")
function(test_function_return)
    message("Inside function before return")
    return()
    message("Inside function after return - SHOULD NOT PRINT")
endfunction()

test_function_return()
message("After function call - SHOULD PRINT")

message("")
message("=== Test 2: return() from macro ===")
macro(test_macro_return)
    message("Inside macro before return")
    return()
    message("Inside macro after return - SHOULD NOT PRINT")
endmacro()

# Call macro but check if script continues
# Note: In CMake, return() in a macro returns from the calling scope
set(BEFORE_MACRO "yes")
test_macro_return()
message("After macro call - SHOULD NOT PRINT (macro return exits calling scope)")

# This line should never execute
set(AFTER_MACRO "yes")
