#include "profile_manager.h"

#include <storage/storage.h>
#include <furi.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "ProfileMgr"

// ── Built-in profiles ─────────────────────────────────────────────────────────

static const char* const GRID_DOWN_MSGS[] = {
    "CHECKIN OK", "NEED ASSISTANCE", "MOVING", "HOLD POSITION",
    "ALL CLEAR",  "BATTERY LOW",     "MEDICAL NEEDED", "SHELTER IN PLACE",
};
static const char* const HIKING_MSGS[] = {
    "CHECKIN OK", "ON TRAIL",     "OFF TRAIL",    "SUMMIT REACHED",
    "TURNING BACK", "NEED WATER", "NEED MEDICAL", "CAMP REACHED",
};
static const char* const REDTEAM_MSGS[] = {
    "CHECKIN OK", "IN POSITION",    "MOVING",          "ABORT",
    "PHASE START", "PHASE COMPLETE", "HOLD",            "ALL CLEAR",
};

uint8_t profile_load_builtins(Profile profiles[PROFILE_MAX_COUNT]) {
    strncpy(profiles[0].name, "Grid Down", PROFILE_NAME_LEN - 1);
    profiles[0].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[0].messages[i] = GRID_DOWN_MSGS[i];

    strncpy(profiles[1].name, "Hiking / SAR", PROFILE_NAME_LEN - 1);
    profiles[1].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[1].messages[i] = HIKING_MSGS[i];

    strncpy(profiles[2].name, "Red Team", PROFILE_NAME_LEN - 1);
    profiles[2].message_count = 8;
    for(uint8_t i = 0; i < 8; i++) profiles[2].messages[i] = REDTEAM_MSGS[i];

    return BUILTIN_PROFILE_COUNT;
}

// ── YAML parser helpers ───────────────────────────────────────────────────────

// Trim leading whitespace in-place.
static void ltrim(char* s) {
    size_t n = 0;
    while(s[n] == ' ' || s[n] == '\t') n++;
    if(n > 0) memmove(s, s + n, strlen(s + n) + 1);
}

// Trim trailing whitespace (including CR/LF) in-place.
static void rtrim(char* s) {
    size_t len = strlen(s);
    while(len > 0) {
        char c = s[len - 1];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
            s[--len] = '\0';
        else
            break;
    }
}

static void trimstr(char* s) { ltrim(s); rtrim(s); }

// Remove one layer of surrounding single or double quotes.
static void unquote(char* s) {
    size_t len = strlen(s);
    if(len >= 2 &&
       ((s[0] == '"' && s[len - 1] == '"') ||
        (s[0] == '\'' && s[len - 1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

// Returns false if any character is outside printable ASCII (0x20–0x7E).
static bool is_printable_ascii(const char* s) {
    for(; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if(c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Read one line from the file into buf (max max_len-1 chars).
// Characters beyond max_len-1 are consumed and discarded (long lines are
// truncated, not left dangling in the stream).
// Returns -1 on EOF with no data read; otherwise returns bytes stored.
static int read_yaml_line(File* f, char* buf, size_t max_len) {
    size_t i = 0;
    char c;
    bool any = false;

    while(!storage_file_eof(f)) {
        if(storage_file_read(f, &c, 1) != 1) break;
        any = true;
        if(c == '\n') break;
        if(c == '\r') continue;
        if(i < max_len - 1)
            buf[i++] = c;
        // else: discard — line is longer than our buffer
    }
    buf[i] = '\0';
    return any ? (int)i : -1;
}

// ── YAML profile loader ───────────────────────────────────────────────────────

uint8_t profile_load_yaml(
    Profile* profiles,
    uint8_t  max_profiles,
    char     storage[][PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN + 1])
{
    if(!profiles || max_profiles == 0 || !storage) return 0;

    Storage* store = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(store, GHOSTMESH_SD_DIR);

    File* f = storage_file_alloc(store);

    if(!storage_file_open(f, GHOSTMESH_SD_YAML, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_I(TAG, "No profiles.yaml found");
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
        return 0;
    }

    // cur  = index of the profile currently being built (-1 = none started)
    // loaded = count of profiles whose content has been fully committed
    int cur = -1;
    uint8_t loaded = 0;
    bool cur_has_msgs = false;

    // Line buffer. 80 bytes covers name (≤19) and message (≤22) plus markup.
    char line[80];

    while(true) {
        if(read_yaml_line(f, line, sizeof(line)) < 0) break;  // EOF

        trimstr(line);

        // ── Skip blank lines and comments ──────────────────────────────────
        if(line[0] == '\0' || line[0] == '#') continue;

        // ── "name: <Profile Name>" ─────────────────────────────────────────
        if(strncmp(line, "name:", 5) == 0) {
            // Finalize the previous profile if it had messages.
            if(cur >= 0 && cur_has_msgs) {
                loaded = (uint8_t)(cur + 1);
                cur++;
                if(cur >= (int)max_profiles) {
                    FURI_LOG_W(TAG, "Max %u profiles reached, ignoring rest",
                               (unsigned)max_profiles);
                    break;
                }
            } else if(cur < 0) {
                // First profile.
                cur = 0;
            }
            // else cur >= 0 && !cur_has_msgs: previous profile was empty,
            // reuse its slot rather than wasting a slot on an empty profile.

            // Reset slot.
            cur_has_msgs = false;
            memset(&profiles[cur], 0, sizeof(Profile));

            // Extract and sanitize the name.
            char* name = line + 5;
            trimstr(name);
            unquote(name);
            trimstr(name);

            size_t nlen = strlen(name);
            if(nlen == 0 || nlen > PROFILE_NAME_LEN - 1 ||
               !is_printable_ascii(name)) {
                snprintf(profiles[cur].name, PROFILE_NAME_LEN,
                         "Custom %d", cur + 1);
                FURI_LOG_W(TAG, "Invalid profile name at slot %d, using default", cur);
            } else {
                strncpy(profiles[cur].name, name, PROFILE_NAME_LEN - 1);
                profiles[cur].name[PROFILE_NAME_LEN - 1] = '\0';
            }
            profiles[cur].message_count = 0;
            continue;
        }

        // ── "- <message>" ──────────────────────────────────────────────────
        if(line[0] == '-' && (line[1] == ' ' || line[1] == '\t')) {
            // No profile started yet — silently ignore.
            if(cur < 0) continue;

            // Profile is full — silently cap.
            if(profiles[cur].message_count >= PROFILE_MAX_MESSAGES) continue;

            char* msg = line + 2;
            trimstr(msg);
            unquote(msg);
            trimstr(msg);

            // Validate: non-empty, printable ASCII only.
            if(strlen(msg) == 0 || !is_printable_ascii(msg)) {
                FURI_LOG_W(TAG, "Skipping invalid message in profile %d", cur);
                continue;
            }

            uint8_t idx = profiles[cur].message_count;

            // Truncate to PROFILE_MSG_LEN (silent cap, already announced).
            // LOW-1: strncpy copies up to PROFILE_MSG_LEN bytes and may not NUL-terminate
            // if the source is exactly that length. The explicit NUL at [PROFILE_MSG_LEN]
            // is correct because storage[][idx] is [PROFILE_MSG_LEN + 1] bytes wide.
            strncpy(storage[cur][idx], msg, PROFILE_MSG_LEN);
            storage[cur][idx][PROFILE_MSG_LEN] = '\0';

            profiles[cur].messages[idx] = storage[cur][idx];
            profiles[cur].message_count++;
            cur_has_msgs = true;
            continue;
        }

        // ── Anything else: silently ignore (unknown YAML keys, etc.) ───────
    }

    // Commit the last profile if it had at least one message.
    if(cur >= 0 && cur_has_msgs) {
        loaded = (uint8_t)(cur + 1);
    }

    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);

    FURI_LOG_I(TAG, "Loaded %u custom profile(s) from profiles.yaml",
               (unsigned)loaded);
    return loaded;
}
