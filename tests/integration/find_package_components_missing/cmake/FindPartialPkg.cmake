# FindPartialPkg.cmake - Mock find module that finds only some components

set(PartialPkg_FOUND TRUE)

# Check which components were requested
if(DEFINED PartialPkg_FIND_COMPONENTS)
    foreach(comp ${PartialPkg_FIND_COMPONENTS})
        # Only FoundComp is found, MissingComp is not
        if(comp STREQUAL "FoundComp")
            set(PartialPkg_${comp}_FOUND TRUE)
            message(STATUS "Found component: ${comp}")
        else()
            set(PartialPkg_${comp}_FOUND FALSE)
            message(STATUS "Component ${comp} not found")
        endif()
    endforeach()
endif()
