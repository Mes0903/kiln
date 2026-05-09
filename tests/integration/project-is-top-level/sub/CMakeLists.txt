project(SubProj)

# project() reached via add_subdirectory → not top-level.
if(PROJECT_IS_TOP_LEVEL)
    message(FATAL_ERROR "PROJECT_IS_TOP_LEVEL=${PROJECT_IS_TOP_LEVEL} for subdir project")
endif()
if(SubProj_IS_TOP_LEVEL)
    message(FATAL_ERROR "SubProj_IS_TOP_LEVEL=${SubProj_IS_TOP_LEVEL}")
endif()
# The outer project's per-name var must remain TRUE — these are separate
# variables, indexed by project name.
if(NOT TopProj_IS_TOP_LEVEL)
    message(FATAL_ERROR "TopProj_IS_TOP_LEVEL no longer TRUE inside subdir")
endif()
