#ifndef SLMODF_SHA256_H
#define SLMODF_SHA256_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t state[8]; uint64_t bitlen; uint8_t data[64]; size_t datalen; } SHA256_CTX;
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]);
void sha256_hex(const uint8_t hash[32], char out[65]);
#endif
