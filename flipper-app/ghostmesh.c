#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <string.h>
#include <stdio.h>

#include "helpers/uart_helper.h"
#include "helpers/textmsg_mode.h"
#include "helpers/profile_manager.h"
#include "views/main_view.h"

#define TAG            "GhostMesh"
#define VISIBLE_ROWS   4
#define FEEDBACK_TICKS 10  // × 200 ms = 2 s

typedef struct {
    Gui* gui;
    MainView* main_view;
    UartHelper* uart;
    FuriMutex* mutex;

    // Byte counters (written from UART callback)
    volatile uint32_t rx_bytes;
    volatile uint32_t tx_bytes;

    // RX line accumulation (written from UART callback)
    char rx_buf[64];
    uint8_t rx_buf_len;
    char rx_display[48];
    volatile bool rx_updated;

    // Profiles
    Profile profiles[PROFILE_MAX_COUNT];
    uint8_t profile_count;
    char sd_buf[PROFILE_MAX_MESSAGES][PROFILE_MSG_LEN + 1];

    // Profile selection state
    uint8_t profile_sel;
    uint8_t profile_scroll;

    // Message list state (for the active profile)
    uint8_t msg_sel;
    uint8_t msg_scroll;

    // Send feedback
    char sent_display[24];
    uint8_t feedback_ticks;

    // Which screen is visible
    GhostMeshScreen screen;

    bool running;
} GhostMeshApp;

// ── UART callback ─────────────────────────────────────────────────────────

static void on_rx_byte(uint8_t byte, void* ctx) {
    GhostMeshApp* app = ctx;
    app->rx_bytes++;

    if(byte >= 0x20 && byte < 0x7F) {
        if(app->rx_buf_len < sizeof(app->rx_buf) - 1)
            app->rx_buf[app->rx_buf_len++] = (char)byte;
    } else if(byte == '\n' || byte == '\r') {
        if(app->rx_buf_len > 0) {
            app->rx_buf[app->rx_buf_len] = '\0';
            strncpy(app->rx_display, app->rx_buf, sizeof(app->rx_display) - 1);
            app->rx_display[sizeof(app->rx_display) - 1] = '\0';
            app->rx_updated = true;
            app->rx_buf_len = 0;
        }
    }
}

// ── Input callback ────────────────────────────────────────────────────────

static void on_input(InputKey key, InputType type, void* ctx) {
    GhostMeshApp* app = ctx;

    bool is_nav = (key == InputKeyUp || key == InputKeyDown);
    if(is_nav) {
        if(type != InputTypePress && type != InputTypeRepeat) return;
    } else {
        if(type != InputTypePress) return;
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
            if(app->profile_sel < app->profile_count - 1) {
                app->profile_sel++;
                if(app->profile_sel >= app->profile_scroll + VISIBLE_ROWS)
                    app->profile_scroll = (uint8_t)(app->profile_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk:
            // Load selected profile into the message list and switch screens
            app->msg_sel = 0;
            app->msg_scroll = 0;
            app->screen = GhostMeshScreenMessages;
            break;
        case InputKeyBack:
            app->running = false;
            break;
        default:
            break;
        }
    } else {
        // GhostMeshScreenMessages
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
            if(app->msg_sel < msg_count - 1) {
                app->msg_sel++;
                if(app->msg_sel >= app->msg_scroll + VISIBLE_ROWS)
                    app->msg_scroll = (uint8_t)(app->msg_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk: {
            const char* msg = app->profiles[app->profile_sel].messages[app->msg_sel];
            textmsg_send(app->uart, msg);
            app->tx_bytes += (uint32_t)(strlen(msg) + 1);
            strncpy(app->sent_display, msg, sizeof(app->sent_display) - 1);
            app->sent_display[sizeof(app->sent_display) - 1] = '\0';
            app->feedback_ticks = FEEDBACK_TICKS;
            FURI_LOG_I(TAG, "Sent: %s", msg);
            break;
        }
        case InputKeyBack:
            // Return to profile selection, reset message state
            app->screen = GhostMeshScreenProfile;
            app->msg_sel = 0;
            app->msg_scroll = 0;
            app->feedback_ticks = 0;
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
    app->uart  = uart_helper_alloc(GHOSTMESH_UART_BAUD, on_rx_byte, app);

    // Load built-in profiles
    app->profile_count = profile_load_builtins(app->profiles);

    // Try SD card custom profile
    if(app->profile_count < PROFILE_MAX_COUNT) {
        if(profile_load_sd(&app->profiles[app->profile_count], app->sd_buf)) {
            app->profile_count++;
            FURI_LOG_I(TAG, "Loaded custom profile from SD");
        }
    }

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
    uart_helper_free(app->uart);
    furi_mutex_free(app->mutex);
    free(app);
}

// ── Entry point ───────────────────────────────────────────────────────────

int32_t ghostmesh_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "Starting");

    GhostMeshApp* app = ghostmesh_alloc();

    if(!uart_helper_is_active(app->uart))
        FURI_LOG_E(TAG, "UART acquire failed — another app may be using USART1");

    // Stable array of profile name pointers for the view
    const char* profile_names[PROFILE_MAX_COUNT];
    for(uint8_t i = 0; i < app->profile_count; i++)
        profile_names[i] = app->profiles[i].name;

    MainViewState state = {0};
    state.visible_rows   = VISIBLE_ROWS;
    state.profile_names  = profile_names;
    state.profile_count  = app->profile_count;

    while(app->running) {
        state.screen         = app->screen;
        state.uart_active    = uart_helper_is_active(app->uart);
        state.profile_selected = app->profile_sel;
        state.profile_scroll   = app->profile_scroll;

        // Message screen fields
        Profile* active = &app->profiles[app->profile_sel];
        state.messages       = (const char**)active->messages;
        state.message_count  = active->message_count;
        state.selected_index = app->msg_sel;
        state.scroll_offset  = app->msg_scroll;
        state.rx_bytes       = app->rx_bytes;
        state.tx_bytes       = app->tx_bytes;
        strncpy(state.active_profile_name, active->name,
                sizeof(state.active_profile_name) - 1);

        if(app->rx_updated) {
            strncpy(state.last_rx, app->rx_display, sizeof(state.last_rx) - 1);
            state.last_rx[sizeof(state.last_rx) - 1] = '\0';
            app->rx_updated = false;
        }

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
