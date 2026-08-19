#include "gm_backup.h"
#include "sha256.h"

#include <furi.h>
#include <furi_hal.h> // furi_hal_random_fill_buf, furi_hal_crypto_gcm
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BACKUP_DIR "/ext/apps_data/ghostmesh"

// KDF: key = SHA256^N(salt || passphrase). Simple and trivially mirrored by the Python tool.
static void derive_key(const uint8_t* salt, size_t salt_len, const char* pass, uint8_t key[32]) {
    Sha256Ctx c;
    sha256_init(&c);
    sha256_update(&c, salt, salt_len);
    sha256_update(&c, (const uint8_t*)pass, strlen(pass));
    sha256_final(&c, key);
    for(uint32_t i = 1; i < GM_BACKUP_KDF_ITERS; i++)
        sha256(key, 32, key);
}

bool gm_backup_write(const uint8_t* config, uint16_t config_len,
                     const char* passphrase, uint32_t node_id,
                     char* out_path, size_t out_path_sz) {
    if(!config || config_len == 0 || !passphrase || !*passphrase) return false;

    uint8_t salt[16], iv[12], key[32], tag[16];
    furi_hal_random_fill_buf(salt, sizeof(salt));
    furi_hal_random_fill_buf(iv, sizeof(iv));
    derive_key(salt, sizeof(salt), passphrase, key);

    uint8_t* ct = malloc(config_len);
    if(!ct) {
        memset(key, 0, sizeof(key));
        return false;
    }
    bool ok = furi_hal_crypto_gcm(key, iv, NULL, 0, config, ct, config_len, tag, false);
    memset(key, 0, sizeof(key)); // never leave the derived key in RAM longer than needed
    if(!ok) {
        free(ct);
        return false;
    }

    Storage* store = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(store, BACKUP_DIR);
    snprintf(out_path, out_path_sz, "%s/backup_%04lx.gmb", BACKUP_DIR,
             (unsigned long)(node_id & 0xFFFF));

    File* f = storage_file_alloc(store);
    bool wrote = false;
    if(storage_file_open(f, out_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint8_t hdr[4 + 1 + 4 + 16 + 12 + 16 + 2];
        size_t n = 0;
        memcpy(hdr + n, "GMBK", 4); n += 4;
        hdr[n++] = 1; // version
        uint32_t it = GM_BACKUP_KDF_ITERS;
        hdr[n++] = it & 0xFF; hdr[n++] = (it >> 8) & 0xFF;
        hdr[n++] = (it >> 16) & 0xFF; hdr[n++] = (it >> 24) & 0xFF;
        memcpy(hdr + n, salt, 16); n += 16;
        memcpy(hdr + n, iv, 12); n += 12;
        memcpy(hdr + n, tag, 16); n += 16;
        hdr[n++] = config_len & 0xFF; hdr[n++] = (config_len >> 8) & 0xFF;
        wrote = (storage_file_write(f, hdr, n) == n) &&
                (storage_file_write(f, ct, config_len) == config_len);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    free(ct);
    return wrote;
}
