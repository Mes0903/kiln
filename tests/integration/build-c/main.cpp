#include <iostream>

extern "C" {
int func();
}

int main() {
    std::cout << func() << std::endl;
    return 0;
}
