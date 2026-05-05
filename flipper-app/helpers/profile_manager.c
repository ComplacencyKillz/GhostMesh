#include "profile_manager.h"

#include <storage/storage.h>
#include <furi.h>
#include <string.h>

// ── Built-in message sets ────────────────────────────────────────────────────

static const char* const GRID_DOWN[] = {
    "CHECKIN OK", "NEED ASSISTANCE", "MOVING", "HOLD POSITION",
    "ALL CLEAR",  "BATTERY LOW",     "MEDICAL NEEDED", "SHELTER IN PLACE",
};

static const char* const HIKING[] = {
    "CHECKIN OK", "ON TRAIL",     "OFF TRAIL",    "SUMMIT REACHED",
    "TURNING BACK", "NEED WATER", "NEED MEDICAL", "CAMP REACHED",
};

static const char* const REDTEAM[] = {
    "CHECKIN OK", "IN POSITION",    "MOVING",          "ABORT",
    "PHASE START", "PHASE COMPLETE", "HOLD",            "ALL CLEAR",
};

uint8_t profile_load_builtins(Profile profiles[PROFILE_MAX_COUNT]) {
    // Grid Down
    strncpy(profiles[0].name, "Grid Down", PROFILE_NAME_LEN - 1);
    profiles[0].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[0].messages[i] = GRID_DOWN[i];

    // Hiking / SAR
    strncpy(profiles[1].name, "Hiking / SAR", PROFILE_NAME_LEN - 1);
    profiles[1].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[1].messages[i] = HIKING[i];

    // Red Team Lab
    strncpy(profiles[2].name, "Red Team", PROFILE_NAME_LEN - 1);
    profiles[2].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[2].messages[i] = REDTEAM[i];

    return 3;
}

// ── SD card loader ───────────────────────────────────────────────────────────

static uint16_t read_line(File* file, char* buf, size_t max_len) {
    size_t i = 0;
    char c;
    while(i < max_len - 1) {
        if(storage_file_read(file, &c, 1) != 1) break;
        if(c == '\r') continue;
        if(c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (uint16_t)i;
}

bool profile_load_sd(Profile* out, char msg_storage[][PROFILE_MSG_LEN + 1]) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, GHOSTMESH_SD_DIR);

    File* file = storage_file_alloc(storage);
    bool loaded = false;

    if(storage_file_open(file, GHOSTMESH_SD_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        strncpy(out->name, "Custom (SD)", PROFILE_NAME_LEN - 1);
        out->name[PROFILE_NAME_LEN - 1] = '\0';
        out->message_count = 0;

        char line[PROFILE_MSG_LEN + 4];
        while(out->message_count < PROFILE_MAX_MESSAGES && !storage_file_eof(file)) {
            uint16_t len = read_line(file, line, sizeof(line));
            if(len == 0) continue;
            // Trim to PROFILE_MSG_LEN
            strncpy(msg_storage[out->message_count], line, PROFILE_MSG_LEN);
            msg_storage[out->message_count][PROFILE_MSG_LEN] = '\0';
            out->messages[out->message_count] = msg_storage[out->message_count];
            out->message_count++;
        }
        loaded = (out->message_count > 0);
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return loaded;
}
