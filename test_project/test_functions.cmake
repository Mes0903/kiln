# Test file demonstrating function and macro support

message(STATUS "Testing functions and macros")

# Define a simple function
function(my_function arg1 arg2)
    message(STATUS "Function called with: ${arg1} and ${arg2}")
    set(LOCAL_VAR "This is local")
    message(STATUS "LOCAL_VAR inside function: ${LOCAL_VAR}")
endfunction()

# Define a simple macro
macro(my_macro arg1 arg2)
    message(STATUS "Macro called with: ${arg1} and ${arg2}")
    set(MACRO_VAR "This is from macro")
    message(STATUS "MACRO_VAR inside macro: ${MACRO_VAR}")
endmacro()

# Test scoping differences
set(SHARED_VAR "original")

function(func_test)
    set(SHARED_VAR "modified by function")
    message(STATUS "SHARED_VAR in function: ${SHARED_VAR}")
endfunction()

macro(macro_test)
    set(SHARED_VAR "modified by macro")
    message(STATUS "SHARED_VAR in macro: ${SHARED_VAR}")
endmacro()

# Call the function
my_function("hello" "world")

# Call the macro
my_macro("foo" "bar")

# Test scoping
message(STATUS "SHARED_VAR before: ${SHARED_VAR}")
func_test()
message(STATUS "SHARED_VAR after function: ${SHARED_VAR}")
macro_test()
message(STATUS "SHARED_VAR after macro: ${SHARED_VAR}")

# Test that LOCAL_VAR doesn't leak from function
message(STATUS "LOCAL_VAR outside function: ${LOCAL_VAR}")

# Test that MACRO_VAR DOES leak from macro
message(STATUS "MACRO_VAR outside macro: ${MACRO_VAR}")
