// Uses the library that requires C++20
extern int library_function();

int main() {
    return library_function() == 42 ? 0 : 1;
}
