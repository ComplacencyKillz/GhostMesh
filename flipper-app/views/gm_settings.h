#pragma once
#include <stdint.h>

// Upper bound for the settings value array (GM_SETTING_COUNT is a runtime const, not usable as a C
// array dimension). Keep ≥ the number of GM_SETTINGS entries.
#define GM_SETTINGS_MAX 40

// Data-driven descriptor for the FAP Settings screen. One table (GM_SETTINGS) is the single source
// of truth that drives the draw, the edit/`/set` send, and the `/cfg` bitmask decode — so growing
// the settings list is a table edit, not a change across three hardcoded switch/parse sites.
//
// Bit layout MUST match the firmware's doCfg() (heltec-firmware/CommandModule.cpp):
//   rep bits: 0 arm,1 buzz,2 vib,3 led,4 wipe,5 tilt-bc,6 light-bc,7 prox-bc,8 help,9 status,10 err,11 unknown
//   out bits: 0 led,1 buzz,2 vib,3 screen,4 hbled,5 gpsled ; in bits: 0 tilt,1 light,2 prox,3 ir

typedef enum {
    GM_HEADER, // a non-selectable section divider
    GM_SLIDER, // numeric: value stored directly, edited by +/- step
    GM_TOGGLE, // on/off: value 0/1
    GM_STANCE, // one-touch preset (compound command). Behaviour keyed by `key`: "arm" → /arm//disarm
               // (state from the cfg arm= token); "silent" → /set silent (state = all outputs off);
               // "mode" → 3-state /set mode active|deployed|dormant (state inferred from gps + inputs).
} GmSettingType;

typedef enum {
    GM_MASK_NONE, // slider, header, or a standalone token (e.g. gps=)
    GM_MASK_REP,
    GM_MASK_OUT,
    GM_MASK_IN,
} GmMaskId;

typedef struct {
    GmSettingType type;
    const char*   label; // short (≤9 chars) — the section header gives context
    const char*   key;   // /set key (also the /cfg numeric token for sliders); NULL for headers
    uint16_t      min, max, step; // sliders only
    const char*   unit;           // e.g. " cm", " s", "" — sliders only
    GmMaskId      mask;           // toggles: which /cfg bitmask this bit lives in (NONE = gps token)
    uint8_t       bit;            // toggles: bit index within that mask
} GmSetting;

extern const GmSetting GM_SETTINGS[];
extern const uint8_t   GM_SETTING_COUNT;

// HIBERNATE mode names, index = stored value 0/1/2. Shared by the /set send (ghostmesh.c) and the
// row draw (main_view.c) so the label and the command stay in lockstep.
extern const char* const GM_STANCE_MODES[3];
