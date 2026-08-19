#pragma once

#include <gui/view_port.h>
#include <input/input.h>
#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

#define GHOSTMESH_PROFILE_NAME_LEN 20

typedef enum {
    GhostMeshScreenProfile,    // profile picker on launch; OK → menu
    GhostMeshScreenMenu,       // hub: pick a screen; BACK → profile
    GhostMeshScreenMessages,   // canned message list; BACK → menu
    GhostMeshScreenRxHistory,  // last 16 received messages; BACK → menu
    GhostMeshScreenSensors,    // temp/humidity/pressure/GPS telemetry; BACK → menu
    GhostMeshScreenStatus,     // node state overview; BACK → menu
    GhostMeshScreenControl,    // IR arm/disarm/wipe (Step 2); BACK → menu
} GhostMeshScreen;

typedef struct {
    GhostMeshScreen screen;

    // ── Always relevant ──────────────────────────────────────────────
    bool uart_active;  // true = handshake complete; title bar shows "RDY" vs "..."

    // Battery from device telemetry — shown in the title bar (PWR if 101/powered)
    bool    battery_valid;   // a device_metrics packet has been seen
    uint8_t battery_level;   // 0-100, or 101 = powered/external

    // Environment telemetry — shown on the Sensors screen
    bool  env_valid;     // an environment_metrics packet has been seen
    float temperature;   // degC
    float humidity;      // %RH
    float pressure;      // hPa

    // GPS position — shown on the Sensors screen, logged to CSV
    bool    pos_valid;     // a Position packet has been seen
    int32_t latitude_i;    // degrees * 1e7
    int32_t longitude_i;   // degrees * 1e7
    int32_t altitude;      // meters

    // ── Node armed state (parsed from ARMED/DISARMED mesh text) — Status/Control ──
    bool armed_known;   // an ARMED/DISARMED message has been seen
    bool armed;         // last known arm state of the backpack

    // ── Control page (GhostMeshScreenControl) ────────────────────────
    uint8_t control_selected;      // 0=Arm 1=Disarm 2=Wipe
    bool    wipe_confirm;          // wipe confirmation prompt is showing
    uint8_t wipe_confirm_selected; // 0=Cancel 1=Confirm

    // ── Menu hub (GhostMeshScreenMenu) ───────────────────────────────
    const char** menu_names;
    uint8_t menu_count;
    uint8_t menu_selected;
    uint8_t menu_scroll;

    // ── Profile selection (GhostMeshScreenProfile) ───────────────────
    const char** profile_names;  // array of profile name strings
    uint8_t profile_count;
    uint8_t profile_selected;    // highlighted index
    uint8_t profile_scroll;

    // ── Message list (GhostMeshScreenMessages) ───────────────────────
    uint32_t tx_bytes;  // LOW-3: rx_bytes removed — not meaningful in PROTO mode
    const char** messages;
    uint8_t message_count;
    uint8_t visible_rows;
    uint8_t selected_index;
    uint8_t scroll_offset;
    char active_profile_name[GHOSTMESH_PROFILE_NAME_LEN];
    char last_rx[80];
    bool show_feedback;
    char sent_message[24];

    // ── RX history (GhostMeshScreenRxHistory) ────────────────────────
    const char** history_lines;  // formatted entries, newest first
    uint8_t history_count;
    uint8_t history_scroll;

    // ── Marquee scroll ────────────────────────────────────────────────
    uint8_t scroll_tick;  // incremented each main-loop iteration (~200 ms)
} MainViewState;

typedef struct MainView MainView;

typedef void (*MainViewInputCallback)(InputKey key, InputType type, void* context);

MainView* main_view_alloc(void);
void main_view_free(MainView* view);
ViewPort* main_view_get_view_port(MainView* view);
void main_view_update(MainView* view, const MainViewState* state);
void main_view_set_input_callback(MainView* view, MainViewInputCallback cb, void* context);
