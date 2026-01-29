# FindTestPkg2.cmake - Mock find module that finds some components

set(TestPkg2_FOUND TRUE)

# Check which components were requested
if(DEFINED TestPkg2_FIND_COMPONENTS)
    foreach(comp ${TestPkg2_FIND_COMPONENTS})
        # CompX and CompY are found, CompZ is not
        if(comp STREQUAL "CompX" OR comp STREQUAL "CompY")
            set(TestPkg2_${comp}_FOUND TRUE)
            message(STATUS "Found component: ${comp}")
        else()
            set(TestPkg2_${comp}_FOUND FALSE)
            if(TestPkg2_FIND_REQUIRED_${comp})
                message(STATUS "Required component ${comp} not found")
                set(TestPkg2_FOUND FALSE)
            else()
                message(STATUS "Optional component ${comp} not found")
            endif()
        endif()
    endforeach()
endif()
