#include <iostream>

// This target was created AFTER add_definitions, testing normal application
#ifndef TEST_VALUE
#error "TEST_VALUE not defined"
#endif

#ifndef MODERN_DEF
#error "MODERN_DEF not defined"
#endif

#ifndef COMPILE_OPTION
#error "COMPILE_OPTION not defined"
#endif

int main() {
    std::cout << "after: TEST_VALUE=" << TEST_VALUE
              << " MODERN_DEF=" << MODERN_DEF
              << " COMPILE_OPTION=" << COMPILE_OPTION << std::endl;
    return 0;
}
