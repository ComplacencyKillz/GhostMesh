// BadUSBModule — native USB HID keyboard payload executor for the Heltec V3.
//
// Presents the ESP32-S3's built-in USB OTG as a composite device (CDC serial + HID keyboard) and
// executes DuckyScript payloads from LittleFS (/ghostmesh/). Triggered by mesh (/key @id <name>) or
// IR (NECext 0x05, using whatever name /key last staged). Armed-gated at every entry point.
//
// Build requirements: the Meshtastic build for heltec-v3 must add the following build flags
// (already present in our overridden platformio.ini snippet — see heltec-firmware/README.md):
//   -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0
// With ARDUINO_USB_MODE=1, the framework's USBHIDKeyboard class is available; CDC_ON_BOOT=0
// ensures the serial console runs on the CP2102 (UART0 via GPIO43/44), not on native USB, so
// Meshtastic's serial stack is undisturbed. The ESP32-S3's native USB (GPIO19/20) presents as
// a separate port on the host — the keyboard HID appears there.

#include "BadUSBModule.h"
#include "GhostMeshArming.h"
#include "GhostMeshConfig.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "configuration.h"
#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

BadUSBModule *badUSBModule;
USBHIDKeyboard Keyboard;

#define PAYLOAD_DIR    "/ghostmesh"       // matches CommandModule_payload's PUT_DIR
#define KEY_POLL_MS    10                 // 100 Hz for responsive typing
#define TYPING_DELAY   5                  // ms between keystrokes (stable across OSes)
#define STRING_DELAY   3                  // ms between chars inside STRING blocks

// ── USB HID key codes (subset — the full set is in the framework) ──
// These are the numeric scancodes USBHIDKeyboard::press/release expect.
enum {
    HID_ENTER       = 0x28,
    HID_ESC         = 0x29,
    HID_BACKSPACE   = 0x2A,
    HID_TAB         = 0x2B,
    HID_SPACE       = 0x2C,
    HID_DELETE      = 0x4C,
    HID_UP          = 0x52,
    HID_DOWN        = 0x51,
    HID_LEFT        = 0x50,
    HID_RIGHT       = 0x4F,
    HID_HOME        = 0x4A,
    HID_END         = 0x4D,
    HID_PAGEUP      = 0x4B,
    HID_PAGEDOWN    = 0x4E,
    HID_CAPSLOCK    = 0x39,
    HID_INSERT      = 0x49,
    HID_PRINTSCREEN = 0x46,
    HID_PAUSE       = 0x48,
    HID_MENU        = 0x76,
    // F-keys
    HID_F1 = 0x3A,  HID_F2 = 0x3B,  HID_F3 = 0x3C,  HID_F4 = 0x3D,
    HID_F5 = 0x3E,  HID_F6 = 0x3F,  HID_F7 = 0x40,  HID_F8 = 0x41,
    HID_F9 = 0x42,  HID_F10 = 0x43, HID_F11 = 0x44, HID_F12 = 0x45,
    HID_F13 = 0x68, HID_F14 = 0x69, HID_F15 = 0x6A, HID_F16 = 0x6B,
    HID_F17 = 0x6C, HID_F18 = 0x6D, HID_F19 = 0x6E, HID_F20 = 0x6F,
    HID_F21 = 0x70, HID_F22 = 0x71, HID_F23 = 0x72, HID_F24 = 0x73,
};

// Modifier keycodes for pressReleaseMod
#define MOD_LCTRL  0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT   0x04
#define MOD_LGUI   0x08

struct KeyName {
    const char *name;
    uint8_t code;
    bool isMod;      // true = it's a modifier only (hold by itself, or pass as mod)
    uint8_t mod;     // modifier byte for combo use (0 if not a modifier)
};

static const KeyName KEY_NAMES[] = {
    {"ENTER",       HID_ENTER,       false, 0},
    {"ESC",         HID_ESC,         false, 0},
    {"ESCAPE",      HID_ESC,         false, 0},
    {"BKSP",        HID_BACKSPACE,   false, 0},
    {"BACKSPACE",   HID_BACKSPACE,   false, 0},
    {"TAB",         HID_TAB,         false, 0},
    {"SPACE",       HID_SPACE,       false, 0},
    {"DELETE",      HID_DELETE,      false, 0},
    {"DEL",         HID_DELETE,      false, 0},
    {"UP",          HID_UP,          false, 0},
    {"DOWN",        HID_DOWN,        false, 0},
    {"LEFT",        HID_LEFT,        false, 0},
    {"RIGHT",       HID_RIGHT,       false, 0},
    {"HOME",        HID_HOME,        false, 0},
    {"END",         HID_END,         false, 0},
    {"PAGEUP",      HID_PAGEUP,      false, 0},
    {"PGUP",        HID_PAGEUP,      false, 0},
    {"PAGEDOWN",    HID_PAGEDOWN,    false, 0},
    {"PGDN",        HID_PAGEDOWN,    false, 0},
    {"CAPS",        HID_CAPSLOCK,    false, 0},
    {"CAPSLOCK",    HID_CAPSLOCK,    false, 0},
    {"INSERT",      HID_INSERT,      false, 0},
    {"INS",         HID_INSERT,      false, 0},
    {"PRINTSCREEN", HID_PRINTSCREEN, false, 0},
    {"PAUSE",       HID_PAUSE,       false, 0},
    {"MENU",        HID_MENU,        false, 0},
    {"APP",         HID_MENU,        false, 0},
    {"F1",  HID_F1,  false, 0}, {"F2",  HID_F2,  false, 0}, {"F3",  HID_F3,  false, 0},
    {"F4",  HID_F4,  false, 0}, {"F5",  HID_F5,  false, 0}, {"F6",  HID_F6,  false, 0},
    {"F7",  HID_F7,  false, 0}, {"F8",  HID_F8,  false, 0}, {"F9",  HID_F9,  false, 0},
    {"F10", HID_F10, false, 0}, {"F11", HID_F11, false, 0}, {"F12", HID_F12, false, 0},
    {"F13", HID_F13, false, 0}, {"F14", HID_F14, false, 0}, {"F15", HID_F15, false, 0},
    {"F16", HID_F16, false, 0}, {"F17", HID_F17, false, 0}, {"F18", HID_F18, false, 0},
    {"F19", HID_F19, false, 0}, {"F20", HID_F20, false, 0},
    {"F21", HID_F21, false, 0}, {"F22", HID_F22, false, 0}, {"F23", HID_F23, false, 0},
    {"F24", HID_F24, false, 0},
    // Modifier aliases (for GUI r, CTRL c, ALT F4, SHIFT a style combos)
    {"CTRL",  0, true, MOD_LCTRL},
    {"CONTROL",0, true, MOD_LCTRL},
    {"ALT",   0, true, MOD_LALT},
    {"SHIFT", 0, true, MOD_LSHIFT},
    {"GUI",   0, true, MOD_LGUI},
    {"WINDOWS",0, true, MOD_LGUI},
    {"COMMAND",0, true, MOD_LGUI},
    {nullptr, 0, false, 0},
};

uint8_t BadUSBModule::duckyKeyCode(const char *name) {
    for (const KeyName *k = KEY_NAMES; k->name; k++) {
        if (strcasecmp(name, k->name) == 0)
            return k->code;
    }
    return 0;
}

BadUSBModule::BadUSBModule()
    : SinglePortModule("badusb", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("BadUSB")
{
}

// ── Entry points ────────────────────────────────────────────────────────────────────
// Both mesh and IR call this. Name is a sanitised basename — caller must validate.

bool BadUSBModule::trigger(const char *name)
{
    if (!name || !*name)
        return false;
    if (!ghostmesh_armed) {
        LOG_WARN("BadUSB: trigger '%s' denied — not armed", name);
        return false;
    }
    if (running) {
        LOG_WARN("BadUSB: trigger '%s' ignored — payload '%s' already running", name, curName);
        return false;
    }
    beginPayload(name);
    return running;
}

// ── Open the DuckyScript file, seed the parser ─────────────────────────────────────

void BadUSBModule::beginPayload(const char *name)
{
    if (scriptFile)
        scriptFile.close();

    char path[56];
    snprintf(path, sizeof(path), "%s/%s", PAYLOAD_DIR, name);

    scriptFile = FSCom.open(path, FILE_O_READ);
    if (!scriptFile) {
        LOG_WARN("BadUSB: '%s' not found in %s", name, PAYLOAD_DIR);
        return;
    }

    strncpy(curName, name, sizeof(curName) - 1);
    curName[sizeof(curName) - 1] = '\0';
    running = true;
    parseState = S_IDLE;
    delayUntil = 0;
    lineStart = 0;
    lineBuf[0] = '\0';
    LOG_INFO("BadUSB: executing '%s'", curName);
}

void BadUSBModule::abortPayload()
{
    if (scriptFile)
        scriptFile.close();
    running = false;
    parseState = S_IDLE;
    delayUntil = 0;
    Keyboard.releaseAll();
    LOG_INFO("BadUSB: payload '%s' stopped", curName);
}

// ── Write a string to the HID keyboard buffer, one char at a time, with delay pacing ─

void BadUSBModule::typeString(const char *s)
{
    for (const char *p = s; *p; p++) {
        Keyboard.print(*p);
        delay(STRING_DELAY);
    }
}

void BadUSBModule::pressRelease(uint8_t key)
{
    Keyboard.press(key);
    delay(TYPING_DELAY);
    Keyboard.release(key);
    delay(TYPING_DELAY);
}

void BadUSBModule::pressReleaseMod(uint8_t mod, uint8_t key)
{
    Keyboard.pressRaw(mod);
    delay(TYPING_DELAY);
    Keyboard.press(key);
    delay(TYPING_DELAY);
    Keyboard.release(key);
    delay(TYPING_DELAY);
    Keyboard.releaseRaw(mod);
    delay(TYPING_DELAY);
}

// ── Parser: process one DuckyScript command per tick ────────────────────────────────

void BadUSBModule::stepParser()
{
    if (!running || !scriptFile)
        return;

    uint32_t now = millis();

    // If we're in a DELAY, block typing until the time is up.
    if (parseState == S_DELAY && now < delayUntil)
        return;
    parseState = S_IDLE;

    // Read next line
    char raw[256];
    while (scriptFile.available()) {
        int n = scriptFile.readBytesUntil('\n', raw, sizeof(raw) - 1);
        if (n <= 0)
            continue;
        raw[n] = '\0';

        // Trim trailing \r and whitespace
        char *end = raw + n - 1;
        while (end >= raw && (*end == '\r' || *end == ' '))
            *end-- = '\0';

        // Skip blank lines
        char *s = raw;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == '\0')
            continue;

        // ── REM — skip ──
        if (strncasecmp(s, "REM", 3) == 0 && (s[3] == ' ' || s[3] == '\0'))
            continue;

        // ── DELAY <ms> ──
        if (strncasecmp(s, "DELAY", 5) == 0) {
            uint32_t ms = (uint32_t)strtoul(s + 5, nullptr, 10);
            if (ms > 30000) ms = 30000;
            parseState = S_DELAY;
            delayUntil = now + ms;
            return;
        }

        // ── STRING <text> ──
        if (strncasecmp(s, "STRING", 6) == 0) {
            const char *text = s + 6;
            while (*text == ' ')
                text++;
            typeString(text);
            continue;
        }

        // ── PRINT <text> (alias for STRING, less common but some scripts use it) ──
        if (strncasecmp(s, "PRINT", 5) == 0) {
            const char *text = s + 5;
            while (*text == ' ')
                text++;
            typeString(text);
            continue;
        }

        // ── Single key names (ENTER, ESC, TAB, SPACE, etc.) ──
        // The framework's Keyboard.press() can take 'a'..'z', 'A'..'Z', '0'..'9', ' ', '\n' etc.
        // directly, so common printable keys are handled as simple chars.

        // Check for modifier combos: GUI r, CTRL c, ALT F4, SHIFT a, etc.
        // Syntax: <MOD> <KEY> — the first token is a modifier name, the rest is a key.
        {
            char tok1[24] = {0}, tok2[24] = {0};
            int scanned = sscanf(s, "%23s %23[^\n]", tok1, tok2);
            if (scanned >= 2) {
                // Look up tok1 as a modifier
                uint8_t mod = 0;
                bool isMod = false;
                for (const KeyName *k = KEY_NAMES; k->name; k++) {
                    if (strcasecmp(tok1, k->name) == 0 && k->isMod) {
                        mod = k->mod;
                        isMod = true;
                        break;
                    }
                }
                if (isMod) {
                    uint8_t key = duckyKeyCode(tok2);
                    if (key) {
                        pressReleaseMod(mod, key);
                    } else if (strlen(tok2) == 1) {
                        // Single printable char with modifier
                        Keyboard.pressRaw(mod);
                        delay(TYPING_DELAY);
                        Keyboard.print(tok2[0]);
                        delay(TYPING_DELAY);
                        Keyboard.releaseRaw(mod);
                        delay(TYPING_DELAY);
                    }
                    continue;
                }
            }
        }

        // ── Single standalone keys ──
        {
            uint8_t key = duckyKeyCode(s);
            if (key) {
                pressRelease(key);
                continue;
            }
        }

        // ── Single printable char ──
        if (strlen(s) == 1) {
            Keyboard.print(s[0]);
            delay(TYPING_DELAY);
            continue;
        }

        // Unrecognized — silently skip
    }

    // End of file — payload complete
    abortPayload();
}

// ── runOnce: main loop — step the parser, init USB HID on first boot ───────────────

int32_t BadUSBModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        ghostmesh_config_ensure_loaded();
        // USB HID initialisation — the ESP32-S3's native USB (GPIO19/20) is a separate
        // peripheral from the CP2102 (UART0 on GPIO43/44). TinyUSB composite setup
        // happens automatically when ARDUINO_USB_MODE=1 and Keyboard.begin() is called.
        Keyboard.begin();
        USB.begin();
        LOG_INFO("BadUSB: HID keyboard init (native USB OTG on GPIO19/20)");
    }

    if (running)
        stepParser();

    return KEY_POLL_MS;
}