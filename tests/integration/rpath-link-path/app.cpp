#include <cstdio>
extern "C" int my_value();
int main() {
    std::printf("%d\n", my_value());
    return my_value() == 43 ? 0 : 1;
}
