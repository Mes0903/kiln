#include <iostream>

extern int foo();
extern int bar();
extern "C" int baz();

int main() {
    std::cout << "foo() = " << foo() << std::endl;
    std::cout << "bar() = " << bar() << std::endl;
    std::cout << "baz() = " << baz() << std::endl;
    return 0;
}
