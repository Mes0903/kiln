cmake_minimum_required(VERSION 3.15)
project(PropertySystemTest)

# Test define_property() and property operations

# Define a custom global property
define_property(GLOBAL
    PROPERTY MY_GLOBAL_PROP
    BRIEF_DOCS "A custom global property"
    FULL_DOCS "This is a test property for global scope"
)

# Define a custom directory property with inheritance
define_property(DIRECTORY
    PROPERTY MY_DIR_PROP INHERITED
    BRIEF_DOCS "A custom directory property"
)

# Define a custom target property
define_property(TARGET
    PROPERTY MY_TARGET_PROP
    BRIEF_DOCS "A custom target property"
)

# Define a custom source property with inheritance
define_property(SOURCE
    PROPERTY MY_SOURCE_PROP INHERITED
    BRIEF_DOCS "A custom source property"
)

# Test GLOBAL properties
set_property(GLOBAL PROPERTY MY_GLOBAL_PROP "GlobalValue")
get_property(global_val GLOBAL PROPERTY MY_GLOBAL_PROP)
message(STATUS "Global property: ${global_val}")

# Test GLOBAL property DEFINED query
get_property(is_defined GLOBAL PROPERTY MY_GLOBAL_PROP DEFINED)
message(STATUS "Global property defined: ${is_defined}")

# Test GLOBAL property SET query
get_property(is_set GLOBAL PROPERTY MY_GLOBAL_PROP SET)
message(STATUS "Global property set: ${is_set}")

# Test GLOBAL property APPEND
set_property(GLOBAL APPEND PROPERTY MY_GLOBAL_PROP "AppendedValue")
get_property(global_val_appended GLOBAL PROPERTY MY_GLOBAL_PROP)
message(STATUS "Global property after append: ${global_val_appended}")

# Test DIRECTORY properties
set_property(DIRECTORY PROPERTY MY_DIR_PROP "DirValue")
get_property(dir_val DIRECTORY PROPERTY MY_DIR_PROP)
message(STATUS "Directory property: ${dir_val}")

# Test TARGET properties
add_library(testlib STATIC test.cpp)
set_property(TARGET testlib PROPERTY MY_TARGET_PROP "TargetValue")
get_property(target_val TARGET testlib PROPERTY MY_TARGET_PROP)
message(STATUS "Target property: ${target_val}")

# Test TARGET property APPEND
set_property(TARGET testlib APPEND PROPERTY MY_TARGET_PROP "AppendedTargetValue")
get_property(target_val_appended TARGET testlib PROPERTY MY_TARGET_PROP)
message(STATUS "Target property after append: ${target_val_appended}")

# Test SOURCE properties
set_property(SOURCE test.cpp PROPERTY MY_SOURCE_PROP "SourceValue")
get_property(source_val SOURCE test.cpp PROPERTY MY_SOURCE_PROP)
message(STATUS "Source property: ${source_val}")

# Test BRIEF_DOCS and FULL_DOCS retrieval
get_property(brief_docs GLOBAL PROPERTY MY_GLOBAL_PROP BRIEF_DOCS)
message(STATUS "Global property brief docs: ${brief_docs}")

get_property(full_docs GLOBAL PROPERTY MY_GLOBAL_PROP FULL_DOCS)
message(STATUS "Global property full docs: ${full_docs}")

# Test property not found
get_property(not_found GLOBAL PROPERTY NONEXISTENT_PROP)
if(NOT not_found)
    message(STATUS "Property not found correctly returns empty")
endif()

# Test INTERFACE properties via set_property
set_property(TARGET testlib PROPERTY INTERFACE_COMPILE_DEFINITIONS "TEST_DEFINE=1")
get_property(iface_def TARGET testlib PROPERTY INTERFACE_COMPILE_DEFINITIONS)
message(STATUS "Interface property: ${iface_def}")

message(STATUS "=== Property System Test Complete ===")
