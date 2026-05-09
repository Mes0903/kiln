# While this file is being interpreted, CMAKE_PARENT_LIST_FILE must point
# at the file that called include() on us — i.e. script.cmake.
if(NOT "${CMAKE_PARENT_LIST_FILE}" STREQUAL "${_outer_file}")
    message(FATAL_ERROR
        "inside included.cmake: CMAKE_PARENT_LIST_FILE=${CMAKE_PARENT_LIST_FILE} "
        "expected ${_outer_file}")
endif()
