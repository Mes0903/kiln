# FindTestPkg.cmake - Mock find module that finds all components

set(TestPkg_FOUND TRUE)

# Check which components were requested
if(DEFINED TestPkg_FIND_COMPONENTS)
    foreach(comp ${TestPkg_FIND_COMPONENTS})
        # For this test, we'll mark all components as found
        set(TestPkg_${comp}_FOUND TRUE)

        if(TestPkg_FIND_REQUIRED_${comp})
            message(STATUS "Found required component: ${comp}")
        else()
            message(STATUS "Found optional component: ${comp}")
        endif()
    endforeach()
endif()
