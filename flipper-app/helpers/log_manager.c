#include "log_manager.h"

#include <storage/storage.h>
#include <furi.h>
#include <stdio.h>
#include <string.h>

#define TAG     "LogMgr"
#define LOG_DIR "/ext/apps_data/ghostmesh"

// Replace any double-quotes in src with single-quotes so the message field
// is safe to wrap in CSV double-quotes without escaping.
static void sanitize_csv_field(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;
    while(*src && i < dst_size - 1) {
        char c = *src++;
        dst[i++] = (c == '"') ? '\'' : c;
    }
    dst[i] = '\0';
}

void log_rx_message(const char* sender, const char* text,
                    int16_t rssi, float snr, const DateTime* dt,
                    bool has_pos, int32_t lat_i, int32_t lon_i) {
    char path[64];
    snprintf(path, sizeof(path), "%s/log_%04u%02u%02u.csv",
             LOG_DIR, (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day);

    Storage* store = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(store, LOG_DIR);

    File* f = storage_file_alloc(store);

    // Probe for existence before opening for append — determines whether to
    // write the header row.
    bool existed = storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING);
    storage_file_close(f);

    if(!storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        FURI_LOG_E(TAG, "Cannot open log: %s", path);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    if(!existed) {
        const char* hdr = "timestamp,node_id,message,lat,lon,rssi,snr\n";
        storage_file_write(f, hdr, strlen(hdr));
    }

    char safe_text[48];
    sanitize_csv_field(safe_text, text, sizeof(safe_text));

    // Last-known GPS fix → lat/lon columns (deg*1e7 → degrees); blank if no fix.
    char lat_s[20], lon_s[20];
    if(has_pos) {
        snprintf(lat_s, sizeof(lat_s), "%.7f", (double)lat_i / 10000000);
        snprintf(lon_s, sizeof(lon_s), "%.7f", (double)lon_i / 10000000);
    } else {
        lat_s[0] = '\0';
        lon_s[0] = '\0';
    }

    char row[160];
    int n = snprintf(row, sizeof(row),
                     "%04u-%02u-%02uT%02u:%02u:%02u,%s,\"%s\",%s,%s,%d,%.1f\n",
                     (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day,
                     (unsigned)dt->hour, (unsigned)dt->minute, (unsigned)dt->second,
                     sender, safe_text, lat_s, lon_s, (int)rssi, (double)snr);

    if(n > 0 && (size_t)n < sizeof(row)) {
        storage_file_write(f, row, (uint32_t)n);
    }

    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}
