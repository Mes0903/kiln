// Test C++14 generic lambda feature
int main() {
    auto lambda = [](auto x) { return x * 2; };
    return lambda(21) == 42 ? 0 : 1;
}
