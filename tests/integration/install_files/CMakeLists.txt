cmake_minimum_required(VERSION 3.15)
project(install_files_test)

# Create some files to install
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.txt" "Sample config\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/script.sh" "#!/bin/bash\necho Hello\n")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/config.txt" DESTINATION etc)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/script.sh" DESTINATION bin)
