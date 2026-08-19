#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Encrypted config backup.
//
// Derives an AES-256 key from `passphrase` (iterated SHA-256 over salt||passphrase), encrypts the
// captured config buffer with AES-256-GCM, and writes an encrypted .gmb file to
// /ext/apps_data/ghostmesh/backup_<nodeid>.gmb. The passphrase is never stored — a captured
// Flipper yields only ciphertext. Restore with tools/restore_backpack.py using the same passphrase.
//
// File format (little-endian):
//   "GMBK"(4) version(1) kdf_iters(4) salt(16) iv(12) tag(16) ct_len(2) ciphertext(ct_len)
#define GM_BACKUP_KDF_ITERS 50000u

// Returns true on success; out_path receives the written path.
bool gm_backup_write(const uint8_t* config, uint16_t config_len,
                     const char* passphrase, uint32_t node_id,
                     char* out_path, size_t out_path_sz);
