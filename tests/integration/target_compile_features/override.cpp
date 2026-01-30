// Even though CXX_STANDARD is set to 11, compile features require 17
#include <optional>

int main() {
    std::optional<int> opt = 42;
    return 0;
}
