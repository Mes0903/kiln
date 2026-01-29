#include "mylib.hpp"
#include "utils.hpp"
#include <iostream>

int main() {
    int value = get_value();
    int sum = add(value, 8);

    std::cout << "Value: " << value << std::endl;
    std::cout << "Sum: " << sum << std::endl;

    if (sum == 50) {
        std::cout << "SUCCESS: All tests passed!" << std::endl;
        return 0;
    } else {
        std::cerr << "ERROR: Expected sum=50, got " << sum << std::endl;
        return 1;
    }
}
