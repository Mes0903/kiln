# FindMockModule.cmake - test that find_path respects <PackageName>_ROOT

find_path(MOCKMODULE_INCLUDE_DIR NAMES mockroot.h)

if(MOCKMODULE_INCLUDE_DIR)
    set(MockModule_FOUND TRUE)
    message(STATUS "FindMockModule: found header at ${MOCKMODULE_INCLUDE_DIR}")
else()
    set(MockModule_FOUND FALSE)
    message(STATUS "FindMockModule: header not found")
endif()
