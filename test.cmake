
find_package(PkgConfig REQUIRED)
pkg_check_modules(Magick++ REQUIRED IMPORTED_TARGET Magick++)
cmake_dump_target_info(PkgConfig::Magick++)
