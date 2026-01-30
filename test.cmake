
set(COMPILATION_FLAGS "-Wall -Wextra -Werror -pedantic")
set(DROGON_CXX_STANDARD 17)
configure_file(
    "DrogonConfig.cmake.in"
    "DrogonConfig.cmake"
    @ONLY
)
