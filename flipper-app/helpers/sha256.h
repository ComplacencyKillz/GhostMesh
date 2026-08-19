#pragma once
#include <stdint.h>
#include <stddef.h>

// Minimal public-domain SHA-256. Bundled because the Flipper SDK does not export a hash to FAPs,
// and the config-backup KDF must produce identical keys to the Python restore tool.

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} Sha256Ctx;

void sha256_init(Sha256Ctx* c);
void sha256_update(Sha256Ctx* c, const uint8_t* data, size_t len);
void sha256_final(Sha256Ctx* c, uint8_t out[32]);
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
