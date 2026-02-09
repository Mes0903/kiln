#include <stdio.h>
extern int add_numbers(int a, int b);
int main() {
    int result = add_numbers(3, 4);
    if (result != 7) {
        printf("FAIL: expected 7 got %d\n", result);
        return 1;
    }
    printf("ASM test passed: %d\n", result);
    return 0;
}
