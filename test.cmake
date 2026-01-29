add_library(mylib)
include(GenerateExportHeader)
generate_export_header(mylib
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/mylib_export.h"
    EXPORT_MACRO_NAME MYLIB_EXPORT
    STATIC_DEFINE MYLIB_STATIC
)
get_property(type TARGET mylib PROPERTY TYPE)
message(STATUS "Library type: ${type}")

if(NOT ${type} STREQUAL "STATIC_LIBRARY"
       AND NOT ${type} STREQUAL "SHARED_LIBRARY"
       AND NOT ${type} STREQUAL "OBJECT_LIBRARY"
       AND NOT ${type} STREQUAL "MODULE_LIBRARY")
    message(FATAL_ERROR "mylib must be STATIC_LIBRARY, SHARED_LIBRARY, OBJECT_LIBRARY, or MODULE_LIBRARY")
endif()
