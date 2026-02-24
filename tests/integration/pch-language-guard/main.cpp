#include <cstdio>
#include <string>
#include "removed_api.h"

extern std::string hello();
extern "C" int add(int a, int b);

int main() {
    // Verify C++ PCH works (hello uses string/vector from PCH)
    auto h = hello();

    // Verify C linkage works (util.c compiled without C++ PCH)
    int sum = add(1, 2);

    // Verify SKIP_PRECOMPILE_HEADERS works (removed_api.cpp defines macro before includes)
    int val = removed_value();

    if (h == "hello" && sum == 3 && val == 42) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL: h=%s sum=%d val=%d\n", h.c_str(), sum, val);
    return 1;
}
