/*
SHA-1 in C
By Steve Reid <steve@edmweb.com>
100% Public Domain
*/

#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>
#include <stddef.h>

#define SHA1_BLOCK_SIZE 20  // SHA1 outputs a 20 byte digest

typedef struct
{
    uint32_t state[5];
    size_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

void kiln_sha1_init(SHA1_CTX *context);
void kiln_sha1_update(SHA1_CTX *context, const unsigned char *data, size_t len);
void kiln_sha1_final(unsigned char digest[20], SHA1_CTX *context);

#endif  // SHA1_H
