#include <iostream>

// This target was created BEFORE add_definitions, testing retroactive application
#ifndef TEST_VALUE
#error "TEST_VALUE not defined (retroactive add_definitions failed)"
#endif

#ifndef MODERN_DEF
#error "MODERN_DEF not defined (retroactive add_compile_definitions failed)"
#endif

#ifndef COMPILE_OPTION
#error "COMPILE_OPTION not defined (retroactive add_compile_options failed)"
#endif

int main() {
    std::cout << "before: TEST_VALUE=" << TEST_VALUE
              << " MODERN_DEF=" << MODERN_DEF
              << " COMPILE_OPTION=" << COMPILE_OPTION << std::endl;
    return 0;
}
