# Standard CMake version file pattern
set(PACKAGE_VERSION "2.3.4")

if(PACKAGE_FIND_VERSION_MAJOR EQUAL 2)
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
else()
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
endif()

# Exact match requires all components to match
if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
else()
    set(PACKAGE_VERSION_EXACT FALSE)
endif()
