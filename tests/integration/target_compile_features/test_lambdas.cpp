// Test C++11 lambda feature
int main() {
    auto lambda = [](int x) { return x * 2; };
    return lambda(21) == 42 ? 0 : 1;
}
