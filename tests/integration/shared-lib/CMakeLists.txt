project(SharedLibTest)
add_library(mylib SHARED mylib.cpp)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)
