# Generates kiln/version.cpp from cmake/version.cpp.in by querying git.
# Designed to be run via `cmake -P` from a custom target every build.
# Required vars: SRC_DIR, IN_FILE, OUT_FILE
#
# Writes OUT_FILE only if its contents would change, so unrelated tags or
# clean/dirty flips do not retrigger compilation downstream.

set(KILN_VERSION "0.0.0")
set(KILN_VERSION_FULL "0.0.0")

find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE _git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_result
        ERROR_QUIET
    )
    if(_git_result EQUAL 0 AND _git_describe)
        set(KILN_VERSION_FULL "${_git_describe}")
        # Extract leading semver "X.Y.Z" (allow optional leading 'v').
        if(_git_describe MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
            set(KILN_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()

set(_tmp "${OUT_FILE}.tmp")
configure_file("${IN_FILE}" "${_tmp}" @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_tmp}" "${OUT_FILE}")
file(REMOVE "${_tmp}")
