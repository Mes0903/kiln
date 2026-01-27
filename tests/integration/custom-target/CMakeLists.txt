project(CustomTargetIntegrationTest)

# Test simple echo and multiple commands
add_custom_target(simple_target
    COMMAND ${CMAKE_COMMAND} -E echo "Hello World"
    COMMAND ${CMAKE_COMMAND} -E touch produced_file.txt
    COMMENT "Running simple target"
)

# Test ALL flag
add_custom_target(all_target ALL
    COMMAND ${CMAKE_COMMAND} -E echo "All target running"
    COMMAND ${CMAKE_COMMAND} -E touch all_produced.txt
)

# Test dependencies
add_custom_target(dependent_target
    COMMAND ${CMAKE_COMMAND} -E echo "Dependent target running"
    COMMAND ${CMAKE_COMMAND} -E touch dependent_produced.txt
    DEPENDS simple_target
)
