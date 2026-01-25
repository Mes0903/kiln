#pragma once

#include <cstddef>
#include <unistd.h>
#include <string>

namespace dmake {
struct Hash256
{
    unsigned char bytes[32];

    std::string to_string() const;
};

/**
 * @brief Compute the BLAKE2b hash of the given data
 * @note When in doubt, use SHA3 or BLAKE2b. Both are safe and SHA3 is faster if
 * you are using OpenSSL and it has SHA3 in hardware mode. Otherwise BLAKE2b is
 * faster in software.
 */
Hash256 blake2b(const void *data, size_t len, const void* key, size_t keylen);
inline Hash256 blake2b(std::string_view str, std::string_view key="")
{
    return blake2b(str.data(), str.size(), key.data(), key.size());
}


}
