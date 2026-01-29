cmake_minimum_required(VERSION 3.15)
project(ConfigureFileTest VERSION 1.2.3)

# Set up some variables
set(PROJECT_NAME "TestProject")
set(VERSION "1.2.3")
set(FEATURE_ENABLED ON)
set(FEATURE_DISABLED OFF)
set(BOOL_FEATURE TRUE)
set(VALUE_FEATURE "CustomValue")

# Test basic configure_file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

# Test @ONLY mode (should not expand ${} variables)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/script.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/script.sh
    @ONLY
)

# Test with USE_SOURCE_PERMISSIONS (default)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config_perms.h
    USE_SOURCE_PERMISSIONS
)

# Test with NO_SOURCE_PERMISSIONS (644)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config_no_perms.h
    NO_SOURCE_PERMISSIONS
)

# Create a dummy executable so the build succeeds
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/main.cpp "int main() { return 0; }\n")
add_executable(dummy ${CMAKE_CURRENT_BINARY_DIR}/main.cpp)
