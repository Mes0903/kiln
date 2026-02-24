cmake_minimum_required(VERSION 3.20)
project(FileGenDep CXX)

add_library(mylib SHARED empty.cpp)

# Property controls what goes in the generated file
set(EXTRA_CONTENT "$<TARGET_PROPERTY:mylib,_my_custom_prop>")
set(HAS_EXTRA "$<BOOL:${EXTRA_CONTENT}>")

set(contents "line1\n")
string(APPEND contents "$<${HAS_EXTRA}:${EXTRA_CONTENT}\n>")
string(APPEND contents "line_end\n")

set(infile "${CMAKE_CURRENT_BINARY_DIR}/generated.in")
set(outfile "${CMAKE_CURRENT_BINARY_DIR}/generated.txt")

file(GENERATE OUTPUT "${infile}" CONTENT "${contents}")

# Custom command with an EXPLICIT dependency on stable_dep.txt,
# but NO explicit dependency on generated.in.
# Kiln must detect the implicit dependency on generated.in from the COMMAND args.
add_custom_command(
    OUTPUT "${outfile}"
    COMMAND ${CMAKE_COMMAND} -E copy "${infile}" "${outfile}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stable_dep.txt"
    COMMENT "Copying generated file"
    VERBATIM
)
add_custom_target(gen_target ALL DEPENDS "${outfile}")
