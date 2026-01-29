
  set(X11_INC_SEARCH_PATH
    /usr/pkg/xorg/include
    /usr/X11R6/include
    /usr/X11R7/include
    /usr/include/X11
    /usr/openwin/include
    /usr/openwin/share/include
    /opt/graphics/OpenGL/include
    /opt/X11/include
  )
find_path(X11_X11_INCLUDE_PATH X11/X.h                             ${X11_INC_SEARCH_PATH})
