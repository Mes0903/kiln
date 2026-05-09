# Verifies the builtin variables that kiln populates at startup. Each block
# uses if(NOT ...)/FATAL_ERROR so any regression breaks the test with a
# non-zero exit.

# Defaults that should be set on every host
foreach(v
    CMAKE_HOME_DIRECTORY
    CMAKE_INSTALL_DEFAULT_COMPONENT_NAME
    CMAKE_C_OUTPUT_EXTENSION
    CMAKE_CXX_OUTPUT_EXTENSION
    CMAKE_FIND_LIBRARY_PREFIXES
    CMAKE_FIND_LIBRARY_SUFFIXES
    CMAKE_PARENT_LIST_FILE
    CMAKE_HOST_SYSTEM_VERSION
    CMAKE_HOST_SYSTEM
    CMAKE_SYSTEM_VERSION
    CMAKE_SYSTEM)
    if("${${v}}" STREQUAL "")
        message(FATAL_ERROR "${v} is unset")
    endif()
endforeach()

# Exact-value checks
if(NOT CMAKE_INSTALL_DEFAULT_COMPONENT_NAME STREQUAL "Unspecified")
    message(FATAL_ERROR "CMAKE_INSTALL_DEFAULT_COMPONENT_NAME=${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME}")
endif()
if(NOT CMAKE_C_OUTPUT_EXTENSION STREQUAL ".o")
    message(FATAL_ERROR "CMAKE_C_OUTPUT_EXTENSION=${CMAKE_C_OUTPUT_EXTENSION}")
endif()

# Linux-specific gates: the runner is Linux-only today, so CMAKE_HOST_LINUX
# must be set and the lib pattern lists must include the GNU shared/static
# extensions.
if(NOT CMAKE_HOST_LINUX)
    message(FATAL_ERROR "CMAKE_HOST_LINUX not set on linux host")
endif()
string(FIND "${CMAKE_FIND_LIBRARY_SUFFIXES}" ".so" _so_pos)
string(FIND "${CMAKE_FIND_LIBRARY_SUFFIXES}" ".a"  _a_pos)
if(_so_pos EQUAL -1 OR _a_pos EQUAL -1)
    message(FATAL_ERROR "CMAKE_FIND_LIBRARY_SUFFIXES missing .so/.a: ${CMAKE_FIND_LIBRARY_SUFFIXES}")
endif()

# CMAKE_HOST_SYSTEM should be "<name>-<version>"
if(NOT CMAKE_HOST_SYSTEM STREQUAL "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_VERSION}")
    message(FATAL_ERROR "CMAKE_HOST_SYSTEM=${CMAKE_HOST_SYSTEM} expected ${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_VERSION}")
endif()

# In script mode the top-level file IS the script; PARENT_LIST_FILE points
# at the includer (kiln seeds it to the top-level CMakeLists.txt fallback).
if(CMAKE_PARENT_LIST_FILE STREQUAL "")
    message(FATAL_ERROR "CMAKE_PARENT_LIST_FILE empty")
endif()

# include() must swap PARENT_LIST_FILE while the included file runs and
# restore it after.
set(_outer_file "${CMAKE_CURRENT_LIST_FILE}")
set(_outer_parent "${CMAKE_PARENT_LIST_FILE}")
include("${CMAKE_CURRENT_LIST_DIR}/included.cmake")
if(NOT "${CMAKE_PARENT_LIST_FILE}" STREQUAL "${_outer_parent}")
    message(FATAL_ERROR "CMAKE_PARENT_LIST_FILE not restored after include()")
endif()
if(NOT "${CMAKE_CURRENT_LIST_FILE}" STREQUAL "${_outer_file}")
    message(FATAL_ERROR "CMAKE_CURRENT_LIST_FILE not restored after include()")
endif()

message(STATUS "builtin-vars: OK")
