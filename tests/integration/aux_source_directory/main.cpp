#include <iostream>

// Declarations from src/ files
extern "C" int baz();
int foo();
int bar();

int main() {
    std::cout << "foo() = " << foo() << std::endl;
    std::cout << "bar() = " << bar() << std::endl;
    std::cout << "baz() = " << baz() << std::endl;
    return 0;
}
