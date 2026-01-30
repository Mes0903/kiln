#include "config.h"
#include <iostream>
#include <string>

int main() {
    // Check that COMPILATION_FLAGS was defined
#ifndef COMPILATION_FLAGS
    std::cerr << "ERROR: COMPILATION_FLAGS should be defined\n";
    return 1;
#endif

    std::string expected_flags = "-Wall -Wextra -Werror";
    std::string actual_flags = COMPILATION_FLAGS;
    if (actual_flags != expected_flags) {
        std::cerr << "ERROR: COMPILATION_FLAGS mismatch\n";
        std::cerr << "  Expected: " << expected_flags << "\n";
        std::cerr << "  Actual: " << actual_flags << "\n";
        return 1;
    }

    // Check that CXX_STANDARD was defined with adjacent variable references
#ifndef CXX_STANDARD
    std::cerr << "ERROR: CXX_STANDARD should be defined\n";
    return 1;
#endif

    std::string expected_combined = "-Wall -Wextra -Werror17";
    std::string actual_combined = CXX_STANDARD;
    if (actual_combined != expected_combined) {
        std::cerr << "ERROR: CXX_STANDARD mismatch\n";
        std::cerr << "  Expected: " << expected_combined << "\n";
        std::cerr << "  Actual: " << actual_combined << "\n";
        return 1;
    }

    // Check that CXX_STANDARD_NUM was defined
#ifndef CXX_STANDARD_NUM
    std::cerr << "ERROR: CXX_STANDARD_NUM should be defined\n";
    return 1;
#endif

    if (CXX_STANDARD_NUM != 17) {
        std::cerr << "ERROR: CXX_STANDARD_NUM should be 17, got " << CXX_STANDARD_NUM << "\n";
        return 1;
    }

    // Check that EMPTY_VAR was NOT defined (empty strings are falsy for #cmakedefine)
#ifdef EMPTY_VAR
    std::cerr << "ERROR: EMPTY_VAR should NOT be defined (empty is falsy)\n";
    return 1;
#endif

    // Check that UNDEFINED_VAR was NOT defined
#ifdef UNDEFINED_VAR
    std::cerr << "ERROR: UNDEFINED_VAR should NOT be defined\n";
    return 1;
#endif

    std::cout << "All configure_file tests passed!\n";
    return 0;
}
