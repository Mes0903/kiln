cmake_minimum_required(VERSION 3.15)
project(CustomTargetTest)

# Custom target that runs with ALL
add_custom_target(all_target ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/all_produced.txt
    COMMENT "Running all_target"
)

# Custom target that does NOT run with ALL
add_custom_target(simple_target
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/produced_file.txt
    COMMENT "Running simple_target"
)

# Custom target that depends on simple_target
add_custom_target(dependent_target
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/dependent_produced.txt
    DEPENDS simple_target
    COMMENT "Running dependent_target"
)
