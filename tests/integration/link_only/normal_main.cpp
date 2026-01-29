// This file should compile with MYLIB_ENABLED defined (from mylib's PUBLIC definition)
// and should find mylib.h (from mylib's PUBLIC include directory)

#include "mylib.h"
#include <iostream>

#ifndef MYLIB_ENABLED
#error "MYLIB_ENABLED should be defined via mylib's PUBLIC compile definition"
#endif

int main() {
    std::cout << "Normal app: " << get_value() << std::endl;
    std::cout << "MYLIB_ENABLED: " << MYLIB_ENABLED << std::endl;
    return 0;
}
