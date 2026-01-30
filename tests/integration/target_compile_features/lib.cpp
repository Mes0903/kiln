// Library with C++20 features
#include <concepts>

template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::same_as<T>;
};

int add(Addable auto a, Addable auto b) {
    return a + b;
}

int library_function() {
    return add(20, 22);
}
