# child2: Clears inherited includes via set_property (MariaDB pattern)
get_directory_property(_before INCLUDE_DIRECTORIES)
message(STATUS "child2 before clear: ${_before}")

set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES)

get_directory_property(_after INCLUDE_DIRECTORIES)
message(STATUS "child2 after clear: ${_after}")

# Add own includes after clearing
include_directories(/fake/child2/inc)

get_directory_property(_final INCLUDE_DIRECTORIES)
message(STATUS "child2 final: ${_final}")
