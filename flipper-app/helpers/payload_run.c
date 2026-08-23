#include "payload_run.h"
#include <furi.h>
#include <storage/storage.h>
#include <loader/loader.h>
#include <string.h>
#include <stdio.h>

uint8_t payload_run_scan(char names[][PAYLOAD_NAME_LEN], uint8_t max) {
    uint8_t count = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);

    if(storage_dir_open(dir, PAYLOAD_DIR)) {
        FileInfo info;
        char name[PAYLOAD_NAME_LEN];
        while(count < max && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            strncpy(names[count], name, PAYLOAD_NAME_LEN - 1);
            names[count][PAYLOAD_NAME_LEN - 1] = '\0';
            count++;
        }
    }
    // No PAYLOAD_DIR yet just means nothing is staged — not an error, count stays 0.

    storage_dir_close(dir);
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
    return count;
}

bool payload_run_exists(const char* name) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", PAYLOAD_DIR, name);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);
    return exists;
}

void payload_run_launch(const char* name) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", PAYLOAD_DIR, name);

    Loader* loader = furi_record_open(RECORD_LOADER);
    // "bad_usb" is the built-in app's appid (applications/main/bad_usb/application.fam); passing a
    // file path as args makes it open straight to the run screen for that script, idle — it does NOT
    // fire until the operator presses OK there (see this file's header comment).
    loader_start_detached_with_gui_error(loader, "bad_usb", path);
    furi_record_close(RECORD_LOADER);
}
