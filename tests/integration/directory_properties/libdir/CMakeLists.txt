# Create library in its own subdirectory to avoid retroactive self-linking
add_library(mylib STATIC mylib.cpp)
