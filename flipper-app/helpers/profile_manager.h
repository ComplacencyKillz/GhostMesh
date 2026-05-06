#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PROFILE_MAX_MESSAGES   12
#define PROFILE_MSG_LEN        22
#define PROFILE_NAME_LEN       20

#define BUILTIN_PROFILE_COUNT   3
#define SD_MAX_PROFILES         5
#define PROFILE_MAX_COUNT      (BUILTIN_PROFILE_COUNT + SD_MAX_PROFILES)  // 8 total

#define GHOSTMESH_SD_DIR        "/ext/apps_data/ghostmesh"
#define GHOSTMESH_SD_YAML       "/ext/apps_data/ghostmesh/profiles.yaml"

typedef struct {
    char name[PROFILE_NAME_LEN];
    const char* messages[PROFILE_MAX_MESSAGES];
    uint8_t message_count;
} Profile;

// Fill profiles[0..2] with the 3 built-in profiles. Always returns 3.
uint8_t profile_load_builtins(Profile profiles[PROFILE_MAX_COUNT]);

// Parse /ext/apps_data/ghostmesh/profiles.yaml and load up to max_profiles
// named profile sections. Each section begins with "name: <label>" followed
// by "- <message>" lines. Returns the number of profiles successfully loaded.
//
// storage must be caller-provided: char[SD_MAX_PROFILES][PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN+1]
// Profile.messages[i] will point into storage for all SD-loaded profiles.
uint8_t profile_load_yaml(
    Profile* profiles,
    uint8_t  max_profiles,
    char     storage[][PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN + 1]);
