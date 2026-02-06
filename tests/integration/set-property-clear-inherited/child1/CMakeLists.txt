# child1: Inherits parent includes without clearing
get_directory_property(_inherited INCLUDE_DIRECTORIES)
message(STATUS "child1 inherited: ${_inherited}")

include_directories(/fake/child1/inc)

get_directory_property(_after INCLUDE_DIRECTORIES)
message(STATUS "child1 after add: ${_after}")
