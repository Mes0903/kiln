project(LauncherTest CXX)

# Apply via cache var (the form ccache users typically pass on the command line).
set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/launcher_shim.sh")

add_executable(hello main.cpp)

# Apply via target property too — exercise the per-target override path.
add_executable(hello_target_prop main.cpp)
set_target_properties(hello_target_prop PROPERTIES
    CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/launcher_shim_target.sh")
