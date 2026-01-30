/* Test C99 features */
#include <stdio.h>

#define PRINT_ARGS(...) printf(__VA_ARGS__)

int main() {
    // C99 features: variadic macros, inline, for-loop declarations
    for (int i = 0; i < 1; i++) {
        PRINT_ARGS("C99 works!\n");
    }
    return 0;
}
