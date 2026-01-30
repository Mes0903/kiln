// Test C++17 features
#include <optional>
#include <string_view>

int main() {
    std::optional<int> opt = 42;
    std::string_view sv = "Hello, C++17!";
    return (opt.value_or(0) == 42 && sv.length() > 0) ? 0 : 1;
}
