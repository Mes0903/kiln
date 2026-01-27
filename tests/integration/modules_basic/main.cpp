import Math;
#include <iostream>

int main() {
    // Test basic module functions
    int sum = math::add(5, 3);
    int product = math::multiply(4, 7);
    int sq = math::square(6);

    std::cout << "5 + 3 = " << sum << std::endl;
    std::cout << "4 * 7 = " << product << std::endl;
    std::cout << "6^2 = " << sq << std::endl;

    // Verify results
    if (sum == 8 && product == 28 && sq == 36) {
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Tests failed!" << std::endl;
        return 1;
    }
}
