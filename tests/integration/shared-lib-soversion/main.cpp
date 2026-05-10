#include <iostream>
extern int get_value();
int main() {
    if (get_value() == 42) {
        std::cout << "Success!" << std::endl;
        return 0;
    }
    return 1;
}
