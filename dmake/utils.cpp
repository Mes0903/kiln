#include "utils.hpp"
#include "inner/blake2b.h"
#include <type_traits>

dmake::Hash256 dmake::blake2b(const void *data, size_t len, const void* key, size_t keylen)
{
    Hash256 hash;
    static_assert(sizeof(hash) == 32, "Hash256 must be 32 bytes");
    static_assert(std::is_standard_layout<Hash256>::value, "Hash256 must be a standard layout type");
    dmake_blake2b(&hash, sizeof(hash), data, len, key, keylen);
    return hash;
}

std::string dmake::Hash256::to_string() const
{
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}
