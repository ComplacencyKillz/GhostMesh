#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PROFILE_MAX_MESSAGES  12
#define PROFILE_MSG_LEN       22
#define PROFILE_NAME_LEN      20
#define PROFILE_MAX_COUNT      4  // 3 built-ins + 1 SD card

// SD card path for a user-supplied custom message list (one message per line)
#define GHOSTMESH_SD_DIR   "/ext/apps_data/ghostmesh"
#define GHOSTMESH_SD_FILE  "/ext/apps_data/ghostmesh/custom.txt"

typedef struct {
    char name[PROFILE_NAME_LEN];
    // messages[i] points to either a string literal (built-ins) or sd_buf (SD profile)
    const char* messages[PROFILE_MAX_MESSAGES];
    uint8_t message_count;
} Profile;

// Fill `profiles` with the 3 built-in profiles. Returns the count (always 3).
uint8_t profile_load_builtins(Profile profiles[PROFILE_MAX_COUNT]);

// Try to load a custom profile from GHOSTMESH_SD_FILE into profiles[next_slot].
// msg_storage must be caller-provided: char[PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN+1]
// Returns true if the file existed and at least one message was loaded.
bool profile_load_sd(
    Profile* out,
    char msg_storage[][PROFILE_MSG_LEN + 1]);
