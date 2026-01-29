// This file links with $<LINK_ONLY:mylib>
// It should NOT receive mylib's PUBLIC compile definitions automatically
// We manually add the include path in CMakeLists.txt

#include "mylib.h"
#include <iostream>

// MYLIB_ENABLED should NOT be defined because we used LINK_ONLY
#ifdef MYLIB_ENABLED
#error "MYLIB_ENABLED should NOT be defined when using LINK_ONLY"
#endif

int main() {
    std::cout << "Link-only app: " << get_value() << std::endl;
    std::cout << "MYLIB_ENABLED not defined (as expected with LINK_ONLY)" << std::endl;
    return 0;
}
