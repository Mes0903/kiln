cmake_minimum_required(VERSION 3.15)
project(DirectoryPropertiesTest)

# Test set_directory_properties() and get_directory_property()

# Test setting a single property
set_directory_properties(PROPERTIES MY_PROP "TestValue")
get_directory_property(val MY_PROP)
message(STATUS "Single property: ${val}")

# Test setting multiple properties
set_directory_properties(PROPERTIES
    PROP_A "ValueA"
    PROP_B "ValueB"
    PROP_C "ValueC"
)
get_directory_property(val_a PROP_A)
get_directory_property(val_b PROP_B)
get_directory_property(val_c PROP_C)
message(STATUS "Multiple properties: ${val_a}, ${val_b}, ${val_c}")

# Test overwriting a property
set_directory_properties(PROPERTIES MY_PROP "NewValue")
get_directory_property(new_val MY_PROP)
message(STATUS "Overwritten property: ${new_val}")

# Test empty PROPERTIES (no-op, should not error)
set_directory_properties(PROPERTIES)

# Test getting a non-existent property
get_directory_property(missing_val NONEXISTENT_PROP)
if(NOT missing_val)
    message(STATUS "Non-existent property returns empty")
endif()

# Test PARENT_DIRECTORY property (should be empty for root)
get_directory_property(parent_dir PARENT_DIRECTORY)
if(NOT parent_dir)
    message(STATUS "PARENT_DIRECTORY empty for root")
endif()

# Test get_directory_property with DIRECTORY keyword (current dir)
set_directory_properties(PROPERTIES DIR_KEYWORD_TEST "Works")
get_directory_property(dir_val DIRECTORY . DIR_KEYWORD_TEST)
message(STATUS "DIRECTORY keyword: ${dir_val}")

# Test that properties set via set_property(DIRECTORY ...) are visible
set_property(DIRECTORY PROPERTY VIA_SET_PROPERTY "SetPropertyValue")
get_directory_property(sp_val VIA_SET_PROPERTY)
message(STATUS "Property via set_property: ${sp_val}")

# Test interaction with include_directories (accumulated property)
include_directories(/test/include)
get_directory_property(inc_dirs INCLUDE_DIRECTORIES)
message(STATUS "Include directories: ${inc_dirs}")

# Test DEFINITION keyword (get variable from directory scope)
set(TEST_DEFINITION_VAR "DefinitionValue")
get_directory_property(def_val DEFINITION TEST_DEFINITION_VAR)
message(STATUS "DEFINITION keyword: ${def_val}")

# Process subdirectory to test PARENT_DIRECTORY and cross-directory access
add_subdirectory(subdir)

# After subdirectory, verify we can access subdir's property via DIRECTORY keyword
get_directory_property(subdir_prop DIRECTORY subdir SUBDIR_PROP)
message(STATUS "Subdir property from parent: ${subdir_prop}")

message(STATUS "=== Directory Properties Test Complete ===")
