#include <iostream>
#include "config.h"

// Declared in generated.cpp
int get_value();

int main() {
    std::cout << "Generated value: " << get_value() << std::endl;
    std::cout << "Config value: " << CONFIG_VALUE << std::endl;
    return 0;
}
