#include <iostream>

// Declared in mylib.cpp
int get_lib_value();

// This target should be linked with mylib due to link_libraries()
int main() {
    int value = get_lib_value();
    if (value != 42) {
        std::cerr << "ERROR: Expected 42 from library, got " << value << std::endl;
        return 1;
    }
    std::cout << "linked: Successfully called library function, got " << value << std::endl;
    return 0;
}
