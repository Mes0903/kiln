project(ImportedInterfaceTest)

add_library(my_imported INTERFACE IMPORTED)
target_compile_definitions(my_imported INTERFACE MY_IMPORTED_DEF)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE my_imported)
