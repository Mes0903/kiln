     if(NOT DEFINED DOXYGEN_WARN_FORMAT)
         if(CMAKE_VS_MSBUILD_COMMAND OR CMAKE_VS_DEVENV_COMMAND)
             # The WARN_FORMAT tag determines the format of the warning messages
             # that doxygen can produce. The string should contain the $file,
             # $line and $text tags, which will be replaced by the file and line
             # number from which the warning originated and the warning text.
             # Optionally, the format may contain $version, which will be
             # replaced by the version of the file (if it could be obtained via 
             # FILE_VERSION_FILTER).
             # Doxygen's default value is: $file:$line: $text
             set(DOXYGEN_WARN_FORMAT "$file($line) : $text ")
         endif()
     endif()

