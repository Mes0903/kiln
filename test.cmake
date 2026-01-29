# use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PKG_FONTCONFIG QUIET fontconfig)
endif()
set(Fontconfig_COMPILE_OPTIONS ${PKG_FONTCONFIG_CFLAGS_OTHER})
set(Fontconfig_VERSION ${PKG_FONTCONFIG_VERSION})

find_path( Fontconfig_INCLUDE_DIR
  NAMES
    fontconfig/fontconfig.h
  HINTS
    ${PKG_FONTCONFIG_INCLUDE_DIRS}
    /usr/X11/include
)
message(STATUS "Fontconfig_INCLUDE_DIR = ${Fontconfig_INCLUDE_DIR}")

find_library( Fontconfig_LIBRARY
  NAMES
    fontconfig
  PATHS
    ${PKG_FONTCONFIG_LIBRARY_DIRS}
)

if (Fontconfig_INCLUDE_DIR AND NOT Fontconfig_VERSION)
  file(STRINGS ${Fontconfig_INCLUDE_DIR}/fontconfig/fontconfig.h _contents REGEX "^#define[ \t]+FC_[A-Z]+[ \t]+[0-9]+$")
  unset(Fontconfig_VERSION)
  foreach(VPART MAJOR MINOR REVISION)
    foreach(VLINE ${_contents})
      if(VLINE MATCHES "^#define[\t ]+FC_${VPART}[\t ]+([0-9]+)$")
        set(Fontconfig_VERSION_PART "${CMAKE_MATCH_1}")
        if(Fontconfig_VERSION)
          string(APPEND Fontconfig_VERSION ".${Fontconfig_VERSION_PART}")
        else()
          set(Fontconfig_VERSION "${Fontconfig_VERSION_PART}")
        endif()
      endif()
    endforeach()
  endforeach()
endif ()
