#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <string.h>
#include <stdio.h>

#include "helpers/proto_mode.h"
#include "helpers/profile_manager.h"
#include "helpers/log_manager.h"
#include "views/main_view.h"

#define TAG             "GhostMesh"
#define VISIBLE_ROWS    4
#define FEEDBACK_TICKS  10   // × 200 ms = 2 s
#define RX_HISTORY_MAX  16

typedef struct {
    Gui* gui;
    MainView* main_view;
    ProtoMode* proto;
    FuriMutex* mutex;

    // Byte accounting
    volatile uint32_t tx_bytes;

    // RX (written from UART ISR callback — volatile flag, no mutex; see proto_notes.md)
    char rx_sender[8];
    char rx_text_buf[64];
    int16_t rx_rssi;
    float rx_snr;
    volatile bool rx_updated;

    // RX history ring buffer — newest entry at index 0; only written from main loop
    char rx_history_lines[RX_HISTORY_MAX][48];
    uint8_t rx_history_count;
    uint8_t rx_history_scroll;

    // Profiles
    Profile profiles[PROFILE_MAX_COUNT];
    uint8_t profile_count;
    char sd_buf[SD_MAX_PROFILES][PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN + 1];

    // Profile selector state
    uint8_t profile_sel;
    uint8_t profile_scroll;

    // Message list state
    uint8_t msg_sel;
    uint8_t msg_scroll;

    // Send feedback
    char sent_display[24];
    uint8_t feedback_ticks;

    GhostMeshScreen screen;
    bool running;
} GhostMeshApp;

// ── Proto RX callback ─────────────────────────────────────────────────────
// Called from UART ISR — only stores raw fields and sets the flag.

static void on_rx_text(const char* sender, const char* text,
                       int16_t rssi, float snr, void* ctx) {
    GhostMeshApp* app = ctx;
    strncpy(app->rx_sender, sender, sizeof(app->rx_sender) - 1);
    app->rx_sender[sizeof(app->rx_sender) - 1] = '\0';
    strncpy(app->rx_text_buf, text, sizeof(app->rx_text_buf) - 1);
    app->rx_text_buf[sizeof(app->rx_text_buf) - 1] = '\0';
    app->rx_rssi    = rssi;
    app->rx_snr     = snr;
    app->rx_updated = true;
}

// ── Input callback ────────────────────────────────────────────────────────

static void on_input(InputKey key, InputType type, void* ctx) {
    GhostMeshApp* app = ctx;

    // Nav keys fire on press, repeat, and long; action keys on press only.
    bool is_nav = (key == InputKeyUp || key == InputKeyDown);
    if(is_nav) {
        if(type != InputTypePress && type != InputTypeRepeat && type != InputTypeLong)
            return;
    } else {
        if(type != InputTypePress) return;
    }

    // Long-press Down on the message screen opens RX history.
    if(key == InputKeyDown && type == InputTypeLong) {
        if(app->screen == GhostMeshScreenMessages && app->rx_history_count > 0) {
            app->rx_history_scroll = 0;
            app->screen = GhostMeshScreenRxHistory;
        }
        return;
    }

    if(app->screen == GhostMeshScreenProfile) {
        switch(key) {
        case InputKeyUp:
            if(app->profile_sel > 0) {
                app->profile_sel--;
                if(app->profile_sel < app->profile_scroll)
                    app->profile_scroll = app->profile_sel;
            }
            break;
        case InputKeyDown:
            // MED-3: guard against uint8_t underflow if profile_count == 0
            if(app->profile_count > 0 && app->profile_sel < app->profile_count - 1) {
                app->profile_sel++;
                if(app->profile_sel >= app->profile_scroll + VISIBLE_ROWS)
                    app->profile_scroll = (uint8_t)(app->profile_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk:
            app->msg_sel    = 0;
            app->msg_scroll = 0;
            app->screen     = GhostMeshScreenMessages;
            break;
        case InputKeyBack:
            app->running = false;
            break;
        default:
            break;
        }

    } else if(app->screen == GhostMeshScreenMessages) {
        uint8_t msg_count = app->profiles[app->profile_sel].message_count;
        switch(key) {
        case InputKeyUp:
            if(app->msg_sel > 0) {
                app->msg_sel--;
                if(app->msg_sel < app->msg_scroll)
                    app->msg_scroll = app->msg_sel;
            }
            break;
        case InputKeyDown:
            // MED-2: guard against uint8_t underflow if msg_count == 0
            if(msg_count > 0 && app->msg_sel < msg_count - 1) {
                app->msg_sel++;
                if(app->msg_sel >= app->msg_scroll + VISIBLE_ROWS)
                    app->msg_scroll = (uint8_t)(app->msg_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk: {
            const char* msg = app->profiles[app->profile_sel].messages[app->msg_sel];
            size_t sent = proto_mode_send_text(app->proto, msg);
            if(sent > 0) {
                app->tx_bytes += (uint32_t)sent;
                strncpy(app->sent_display, msg, sizeof(app->sent_display) - 1);
                app->sent_display[sizeof(app->sent_display) - 1] = '\0';
                app->feedback_ticks = FEEDBACK_TICKS;
                FURI_LOG_I(TAG, "Sent: %s (%u bytes)", msg, (unsigned)sent);
            }
            break;
        }
        case InputKeyBack:
            app->screen         = GhostMeshScreenProfile;
            app->msg_sel        = 0;
            app->msg_scroll     = 0;
            app->feedback_ticks = 0;
            break;
        default:
            break;
        }

    } else {  // GhostMeshScreenRxHistory
        switch(key) {
        case InputKeyUp:
            if(app->rx_history_scroll > 0) app->rx_history_scroll--;
            break;
        case InputKeyDown:
            if(app->rx_history_count > VISIBLE_ROWS &&
               app->rx_history_scroll < app->rx_history_count - VISIBLE_ROWS)
                app->rx_history_scroll++;
            break;
        case InputKeyBack:
            app->screen = GhostMeshScreenMessages;
            break;
        default:
            break;
        }
    }
}

// ── App lifecycle ─────────────────────────────────────────────────────────

static GhostMeshApp* ghostmesh_alloc(void) {
    GhostMeshApp* app = malloc(sizeof(GhostMeshApp));
    memset(app, 0, sizeof(GhostMeshApp));
    app->running = true;
    app->screen  = GhostMeshScreenProfile;

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->proto = proto_mode_alloc(GHOSTMESH_UART_BAUD, on_rx_text, app);

    app->profile_count = profile_load_builtins(app->profiles);
    app->profile_count += profile_load_yaml(
        app->profiles + BUILTIN_PROFILE_COUNT,
        SD_MAX_PROFILES,
        app->sd_buf);

    app->main_view = main_view_alloc();
    main_view_set_input_callback(app->main_view, on_input, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, main_view_get_view_port(app->main_view), GuiLayerFullscreen);

    return app;
}

static void ghostmesh_free(GhostMeshApp* app) {
    gui_remove_view_port(app->gui, main_view_get_view_port(app->main_view));
    furi_record_close(RECORD_GUI);
    main_view_free(app->main_view);
    proto_mode_free(app->proto);
    furi_mutex_free(app->mutex);
    free(app);
}

// ── Entry point ───────────────────────────────────────────────────────────

int32_t ghostmesh_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "Starting (PROTO mode)");

    GhostMeshApp* app = ghostmesh_alloc();

    if(!proto_mode_is_active(app->proto))
        FURI_LOG_E(TAG, "UART acquire failed");

    const char* profile_names[PROFILE_MAX_COUNT];
    for(uint8_t i = 0; i < app->profile_count; i++)
        profile_names[i] = app->profiles[i].name;

    // history_ptrs must outlive each main_view_update call; declared here so
    // the draw callback never reads a dead stack frame.
    const char* history_ptrs[RX_HISTORY_MAX];
    uint8_t scroll_tick = 0;

    MainViewState state = {0};
    state.visible_rows  = VISIBLE_ROWS;
    state.profile_names = profile_names;
    state.profile_count = app->profile_count;
    state.history_lines = history_ptrs;

    while(app->running) {
        state.scroll_tick      = scroll_tick++;
        state.screen           = app->screen;
        state.uart_active      = proto_mode_is_connected(app->proto);
        state.profile_selected = app->profile_sel;
        state.profile_scroll   = app->profile_scroll;

        Profile* active      = &app->profiles[app->profile_sel];
        state.messages       = (const char**)active->messages;
        state.message_count  = active->message_count;
        state.selected_index = app->msg_sel;
        state.scroll_offset  = app->msg_scroll;
        state.tx_bytes       = app->tx_bytes;
        strncpy(state.active_profile_name, active->name,
                sizeof(state.active_profile_name) - 1);

        if(app->rx_updated) {
            DateTime dt;
            furi_hal_rtc_get_datetime(&dt);

            // Pre-truncate to a fixed-size local so GCC can prove no overflow.
            char disp[28];
            strncpy(disp, app->rx_text_buf, sizeof(disp) - 1);
            disp[sizeof(disp) - 1] = '\0';

            // Status bar: simple sender: message (no RSSI — see history screen)
            snprintf(state.last_rx, sizeof(state.last_rx),
                     "%.7s: %s", app->rx_sender, disp);
            state.last_rx[sizeof(state.last_rx) - 1] = '\0';

            // History entry: full detail — sender, RSSI, message
            if(app->rx_history_count < RX_HISTORY_MAX)
                app->rx_history_count++;
            memmove(&app->rx_history_lines[1], &app->rx_history_lines[0],
                    (app->rx_history_count - 1) * sizeof(app->rx_history_lines[0]));
            if(app->rx_rssi != 0) {
                snprintf(app->rx_history_lines[0], sizeof(app->rx_history_lines[0]),
                         "%.7s %ddBm: %s", app->rx_sender, (int)app->rx_rssi, disp);
            } else {
                snprintf(app->rx_history_lines[0], sizeof(app->rx_history_lines[0]),
                         "%.7s: %s", app->rx_sender, disp);
            }

            log_rx_message(app->rx_sender, app->rx_text_buf, app->rx_rssi, app->rx_snr, &dt);
            app->rx_updated = false;
        }

        // Rebuild history pointer array (heap strings, always valid)
        for(uint8_t i = 0; i < app->rx_history_count; i++)
            history_ptrs[i] = app->rx_history_lines[i];
        state.history_count  = app->rx_history_count;
        state.history_scroll = app->rx_history_scroll;

        if(app->feedback_ticks > 0) {
            state.show_feedback = true;
            strncpy(state.sent_message, app->sent_display,
                    sizeof(state.sent_message) - 1);
            state.sent_message[sizeof(state.sent_message) - 1] = '\0';
            app->feedback_ticks--;
        } else {
            state.show_feedback = false;
        }

        main_view_update(app->main_view, &state);
        furi_delay_ms(200);
    }

    FURI_LOG_I(TAG, "Exiting");
    ghostmesh_free(app);
    return 0;
}
