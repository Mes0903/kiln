cmake_minimum_required(VERSION 3.15)

# Set a property in the subdirectory
set_directory_properties(PROPERTIES SUBDIR_PROP "SubdirValue")

# Set a variable in this scope
set(SUBDIR_VAR "SubdirVariable")

# Check PARENT_DIRECTORY
get_directory_property(parent_dir PARENT_DIRECTORY)
message(STATUS "Subdir PARENT_DIRECTORY: ${parent_dir}")

# Check we can access parent property via DIRECTORY keyword
get_directory_property(parent_prop DIRECTORY ${parent_dir} MY_PROP)
message(STATUS "Parent property from subdir: ${parent_prop}")
