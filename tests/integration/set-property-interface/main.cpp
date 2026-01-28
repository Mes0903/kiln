// This should find the header because INTERFACE_INCLUDE_DIRECTORIES was propagated
#include "mylib.hpp"
#include <iostream>

int main() {
    std::cout << "Value from set_property: " << get_value() << std::endl;
    return get_value() == 30 ? 0 : 1;
}
