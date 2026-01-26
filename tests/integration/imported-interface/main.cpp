#include <iostream>

int main() {
#ifdef MY_IMPORTED_DEF
    std::cout << "Success!" << std::endl;
    return 0;
#else
    std::cerr << "MY_IMPORTED_DEF not defined!" << std::endl;
    return 1;
#endif
}
