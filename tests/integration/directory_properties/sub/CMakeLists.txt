# This subdirectory should inherit parent's definitions:
# TEST_VALUE, MODERN_DEF, COMPILE_OPTION from parent
# mylib from parent's link_libraries

# Add child-specific definition
add_definitions(-DCHILD_VALUE=400)

# Target created in subdirectory (should have both parent and child properties)
add_executable(child child.cpp)
