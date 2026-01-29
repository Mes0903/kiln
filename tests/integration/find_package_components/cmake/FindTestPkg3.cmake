# FindTestPkg3.cmake - Mock find module that validates component variables

set(TestPkg3_FOUND TRUE)

# Just mark all components as found for simplicity
if(DEFINED TestPkg3_FIND_COMPONENTS)
    foreach(comp ${TestPkg3_FIND_COMPONENTS})
        set(TestPkg3_${comp}_FOUND TRUE)
    endforeach()
endif()

# The CMakeLists.txt will validate that TestPkg3_FIND_COMPONENTS
# and TestPkg3_FIND_REQUIRED_<comp> variables were set correctly
