#include <iostream>
#include "generated.h"

int main() {
    std::cout << "Generated value: " << GENERATED_VALUE << std::endl;
    if (GENERATED_VALUE != 42) {
        std::cerr << "ERROR: Expected 42" << std::endl;
        return 1;
    }
    return 0;
}
