#include "gm_settings.h"

// Order = on-screen order (headers interleaved). Labels ≤9 chars; the header above gives context
// (under "-REPLIES-", "buzz" = the /buzz mesh reply; under "-OUTPUTS-", "buzz" = the physical buzzer).
const GmSetting GM_SETTINGS[] = {
    {GM_HEADER, "-STANCE-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_STANCE, "SENTINEL", "arm", 0, 0, 0, "", GM_MASK_NONE, 0},    // Lt/Rt = disarm/arm
    {GM_STANCE, "BLACKOUT", "silent", 0, 0, 0, "", GM_MASK_NONE, 0}, // Lt/Rt = lit/dark
    {GM_STANCE, "HIBERNAT", "mode", 0, 0, 0, "", GM_MASK_NONE, 0},   // Lt/Rt = cycle active/deployed/dormant

    {GM_HEADER, "-SENSING-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_SLIDER, "prox", "prox", 20, 400, 25, " cm", GM_MASK_NONE, 0},
    {GM_SLIDER, "light", "light", 0, 4095, 100, "", GM_MASK_NONE, 0},

    {GM_HEADER, "-REPLIES-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_TOGGLE, "arm", "rep_arm", 0, 0, 0, "", GM_MASK_REP, 0},
    {GM_TOGGLE, "buzz", "rep_buzz", 0, 0, 0, "", GM_MASK_REP, 1},
    {GM_TOGGLE, "vib", "rep_vib", 0, 0, 0, "", GM_MASK_REP, 2},
    {GM_TOGGLE, "led", "rep_led", 0, 0, 0, "", GM_MASK_REP, 3},
    {GM_TOGGLE, "wipe", "rep_wipe", 0, 0, 0, "", GM_MASK_REP, 4},
    {GM_TOGGLE, "bc tilt", "bc_tilt", 0, 0, 0, "", GM_MASK_REP, 5},
    {GM_TOGGLE, "bc light", "bc_light", 0, 0, 0, "", GM_MASK_REP, 6},
    {GM_TOGGLE, "bc prox", "bc_prox", 0, 0, 0, "", GM_MASK_REP, 7},
    {GM_TOGGLE, "r help", "rep_help", 0, 0, 0, "", GM_MASK_REP, 8},
    {GM_TOGGLE, "r stat", "rep_status", 0, 0, 0, "", GM_MASK_REP, 9},
    {GM_TOGGLE, "r err", "rep_err", 0, 0, 0, "", GM_MASK_REP, 10},
    {GM_TOGGLE, "r unkwn", "rep_unknown", 0, 0, 0, "", GM_MASK_REP, 11},

    {GM_HEADER, "-OUTPUTS-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_TOGGLE, "led", "led", 0, 0, 0, "", GM_MASK_OUT, 0},
    {GM_TOGGLE, "buzz", "buzz", 0, 0, 0, "", GM_MASK_OUT, 1},
    {GM_TOGGLE, "vib", "vib", 0, 0, 0, "", GM_MASK_OUT, 2},
    {GM_TOGGLE, "screen", "screen", 0, 0, 0, "", GM_MASK_OUT, 3},
    {GM_TOGGLE, "onbrd", "hbled", 0, 0, 0, "", GM_MASK_OUT, 4},
    {GM_TOGGLE, "gpsled", "gpsled", 0, 0, 0, "", GM_MASK_OUT, 5},

    {GM_HEADER, "-INPUTS-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_TOGGLE, "tilt", "in_tilt", 0, 0, 0, "", GM_MASK_IN, 0},
    {GM_TOGGLE, "light", "in_light", 0, 0, 0, "", GM_MASK_IN, 1},
    {GM_TOGGLE, "prox", "in_prox", 0, 0, 0, "", GM_MASK_IN, 2},
    {GM_TOGGLE, "ir", "in_ir", 0, 0, 0, "", GM_MASK_IN, 3},

    {GM_HEADER, "-GPS/TEL-", 0, 0, 0, 0, "", GM_MASK_NONE, 0},
    {GM_TOGGLE, "gps", "gps", 0, 0, 0, "", GM_MASK_NONE, 0}, // standalone: decoded from the gps= token
    {GM_SLIDER, "gps int", "gpsint", 0, 3600, 30, " s", GM_MASK_NONE, 0},
    {GM_SLIDER, "tel int", "telint", 0, 3600, 30, " s", GM_MASK_NONE, 0},
};

const uint8_t GM_SETTING_COUNT = sizeof(GM_SETTINGS) / sizeof(GM_SETTINGS[0]);

// HIBERNATE mode names — index = stored value. Sent verbatim as `/set @id mode <name>`.
const char* const GM_STANCE_MODES[3] = {"active", "deployed", "dormant"};
