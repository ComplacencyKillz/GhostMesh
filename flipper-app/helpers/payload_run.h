#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Bad USB payload launch. Payloads are selected by name from PAYLOAD_DIR, never injected.
// A launch request can arrive via mesh "/run @id <name>" or local browse. Hands off to
// Flipper's Bad USB app via Loader — Bad USB's own OK press is what fires the keystrokes.
#define PAYLOAD_DIR       "/ext/badusb"
#define PAYLOAD_MAX_FILES 12
#define PAYLOAD_NAME_LEN  40

// Scan PAYLOAD_DIR for .txt scripts. Returns the count found (capped at PAYLOAD_MAX_FILES); fills
// `names[i]` (each buffer at least PAYLOAD_NAME_LEN bytes) with basenames, empty array if the
// directory doesn't exist yet (nothing staged — not an error).
uint8_t payload_run_scan(char names[][PAYLOAD_NAME_LEN], uint8_t max);

// True if PAYLOAD_DIR/name exists on the SD card.
bool payload_run_exists(const char* name);

// Hand off to Bad USB with PAYLOAD_DIR/name staged (idle, not yet firing — see the file's top
// comment). Caller must have already checked payload_run_exists() and any arming gate; this function
// does not re-check anything, it just launches. The caller should stop its own app's main loop right
// after calling this (loader_start hands off asynchronously; our own app still owns the screen until
// it exits normally).
void payload_run_launch(const char* name);
