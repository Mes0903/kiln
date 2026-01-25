#pragma once

#include <unistd.h>

extern "C" {
void dmake_blake2b(void* output,
                        size_t outlen,
                        const void* input,
                        size_t inlen,
                        const void* key,
                        size_t keylen);
}
