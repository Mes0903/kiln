# Verifies CMake-compatible script-mode argv: every token after the script
# path (and after an optional `--`) shows up as CMAKE_ARGV<N>, with ARGC
# counting kiln + "-P" + script + user args.

if(NOT DEFINED CMAKE_ARGC)
    message(FATAL_ERROR "CMAKE_ARGC not set")
endif()
if(NOT CMAKE_ARGC EQUAL 6)
    message(FATAL_ERROR "expected CMAKE_ARGC=6, got ${CMAKE_ARGC}")
endif()
if(NOT CMAKE_ARGV1 STREQUAL "-P")
    message(FATAL_ERROR "expected CMAKE_ARGV1=-P, got ${CMAKE_ARGV1}")
endif()
# CMAKE_ARGV2 is the script path; only check the basename so the test is
# location-independent.
get_filename_component(_script_name "${CMAKE_ARGV2}" NAME)
if(NOT _script_name STREQUAL "script.cmake")
    message(FATAL_ERROR "CMAKE_ARGV2 basename=${_script_name}")
endif()
if(NOT CMAKE_ARGV3 STREQUAL "alpha")
    message(FATAL_ERROR "CMAKE_ARGV3=${CMAKE_ARGV3}")
endif()
if(NOT CMAKE_ARGV4 STREQUAL "beta gamma")
    message(FATAL_ERROR "CMAKE_ARGV4=${CMAKE_ARGV4}")
endif()
if(NOT CMAKE_ARGV5 STREQUAL "-DLOOKS_LIKE_OPTION=1")
    message(FATAL_ERROR "CMAKE_ARGV5=${CMAKE_ARGV5}")
endif()
if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
    message(FATAL_ERROR "CMAKE_SCRIPT_MODE_FILE not set in script mode")
endif()
message(STATUS "script-args: OK")
