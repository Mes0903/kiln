import std;

int main() {
    std::vector<int> v{1, 2, 3, 4};
    int sum = 0;
    for (int x : v) sum += x;
    std::println("sum={}", sum);
    return sum == 10 ? 0 : 1;
}
