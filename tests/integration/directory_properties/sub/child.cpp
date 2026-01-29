#include <iostream>

// Declared in mylib.cpp (parent directory)
int get_lib_value();

// Should have inherited definitions from parent
#ifndef TEST_VALUE
#error "TEST_VALUE not defined (inheritance from parent failed)"
#endif

#ifndef MODERN_DEF
#error "MODERN_DEF not defined (inheritance from parent failed)"
#endif

#ifndef COMPILE_OPTION
#error "COMPILE_OPTION not defined (inheritance from parent failed)"
#endif

// Should have child-specific definition
#ifndef CHILD_VALUE
#error "CHILD_VALUE not defined (child add_definitions failed)"
#endif

int main() {
    int value = get_lib_value();
    if (value != 42) {
        std::cerr << "ERROR: Expected 42 from library, got " << value << std::endl;
        return 1;
    }

    std::cout << "child: TEST_VALUE=" << TEST_VALUE
              << " MODERN_DEF=" << MODERN_DEF
              << " COMPILE_OPTION=" << COMPILE_OPTION
              << " CHILD_VALUE=" << CHILD_VALUE
              << " lib_value=" << value << std::endl;
    return 0;
}
