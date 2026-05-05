#pragma once

#include <gui/view_port.h>
#include <input/input.h>
#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

#define GHOSTMESH_PROFILE_NAME_LEN 20

typedef enum {
    GhostMeshScreenProfile,
    GhostMeshScreenMessages,
} GhostMeshScreen;

typedef struct {
    GhostMeshScreen screen;

    // ── Always relevant ──────────────────────────────────────────────
    bool uart_active;

    // ── Profile selection (GhostMeshScreenProfile) ───────────────────
    const char** profile_names;  // array of profile name strings
    uint8_t profile_count;
    uint8_t profile_selected;    // highlighted index
    uint8_t profile_scroll;

    // ── Message list (GhostMeshScreenMessages) ───────────────────────
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    const char** messages;
    uint8_t message_count;
    uint8_t visible_rows;
    uint8_t selected_index;
    uint8_t scroll_offset;
    char active_profile_name[GHOSTMESH_PROFILE_NAME_LEN];
    char last_rx[48];
    bool show_feedback;
    char sent_message[24];
} MainViewState;

typedef struct MainView MainView;

typedef void (*MainViewInputCallback)(InputKey key, InputType type, void* context);

MainView* main_view_alloc(void);
void main_view_free(MainView* view);
ViewPort* main_view_get_view_port(MainView* view);
void main_view_update(MainView* view, const MainViewState* state);
void main_view_set_input_callback(MainView* view, MainViewInputCallback cb, void* context);
