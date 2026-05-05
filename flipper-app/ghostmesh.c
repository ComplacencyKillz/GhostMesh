#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <string.h>
#include <stdio.h>

#include "helpers/uart_helper.h"
#include "helpers/textmsg_mode.h"
#include "views/main_view.h"

#define TAG           "GhostMesh"
#define VISIBLE_ROWS  4
#define FEEDBACK_TICKS 10  // × 200 ms = 2 seconds

static const char* const MESSAGES[] = {
    "CHECKIN OK",
    "NEED COMMS CHECK",
    "MOVING",
    "HOLD POSITION",
    "RETURN TO RALLY",
    "BATTERY LOW",
    "CONTACT MADE",
    "ALL CLEAR",
    "ABORT",
    "STANDBY",
};
#define MESSAGE_COUNT ((uint8_t)(sizeof(MESSAGES) / sizeof(MESSAGES[0])))

typedef struct {
    Gui* gui;
    MainView* main_view;
    UartHelper* uart;
    FuriMutex* mutex;

    // Byte counters (written from UART callback, read from main loop)
    volatile uint32_t rx_bytes;
    volatile uint32_t tx_bytes;

    // RX line accumulation (written from UART callback)
    char rx_buf[64];
    uint8_t rx_buf_len;
    char rx_display[48];
    volatile bool rx_updated;

    // Message selection state (written from input callback, read from main loop)
    uint8_t selected;
    uint8_t scroll_offset;

    // Send feedback
    char sent_display[24];
    uint8_t feedback_ticks;

    bool running;
} GhostMeshApp;

// ── UART callback ─────────────────────────────────────────────────────────
// Called from UART driver context — keep short, no blocking calls.

static void on_rx_byte(uint8_t byte, void* ctx) {
    GhostMeshApp* app = ctx;
    app->rx_bytes++;

    if(byte >= 0x20 && byte < 0x7F) {
        if(app->rx_buf_len < sizeof(app->rx_buf) - 1) {
            app->rx_buf[app->rx_buf_len++] = (char)byte;
        }
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
// Called from the GUI thread via the ViewPort input handler.

static void on_input(InputKey key, InputType type, void* ctx) {
    GhostMeshApp* app = ctx;

    // Navigation repeats on hold; actions fire once on press only
    bool is_nav = (key == InputKeyUp || key == InputKeyDown);
    if(is_nav) {
        if(type != InputTypePress && type != InputTypeRepeat) return;
    } else {
        if(type != InputTypePress) return;
    }

    switch(key) {
    case InputKeyUp:
        if(app->selected > 0) {
            app->selected--;
            if(app->selected < app->scroll_offset)
                app->scroll_offset = app->selected;
        }
        break;

    case InputKeyDown:
        if(app->selected < MESSAGE_COUNT - 1) {
            app->selected++;
            if(app->selected >= app->scroll_offset + VISIBLE_ROWS)
                app->scroll_offset = (uint8_t)(app->selected - VISIBLE_ROWS + 1);
        }
        break;

    case InputKeyOk:
        textmsg_send(app->uart, MESSAGES[app->selected]);
        app->tx_bytes += (uint32_t)(strlen(MESSAGES[app->selected]) + 1);
        strncpy(app->sent_display, MESSAGES[app->selected], sizeof(app->sent_display) - 1);
        app->sent_display[sizeof(app->sent_display) - 1] = '\0';
        app->feedback_ticks = FEEDBACK_TICKS;
        FURI_LOG_I(TAG, "Sent: %s", MESSAGES[app->selected]);
        break;

    case InputKeyBack:
        app->running = false;
        break;

    default:
        break;
    }
}

// ── App lifecycle ─────────────────────────────────────────────────────────

static GhostMeshApp* ghostmesh_alloc(void) {
    GhostMeshApp* app = malloc(sizeof(GhostMeshApp));
    memset(app, 0, sizeof(GhostMeshApp));
    app->running = true;

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->uart = uart_helper_alloc(GHOSTMESH_UART_BAUD, on_rx_byte, app);

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

    if(!uart_helper_is_active(app->uart)) {
        FURI_LOG_E(TAG, "UART acquire failed — another app may be using USART1");
    }

    MainViewState state = {0};
    state.messages      = MESSAGES;
    state.message_count = MESSAGE_COUNT;
    state.visible_rows  = VISIBLE_ROWS;

    while(app->running) {
        // Snapshot volatile state for the view
        state.rx_bytes     = app->rx_bytes;
        state.tx_bytes     = app->tx_bytes;
        state.uart_active  = uart_helper_is_active(app->uart);
        state.selected_index = app->selected;
        state.scroll_offset  = app->scroll_offset;

        if(app->rx_updated) {
            strncpy(state.last_rx, app->rx_display, sizeof(state.last_rx) - 1);
            state.last_rx[sizeof(state.last_rx) - 1] = '\0';
            app->rx_updated = false;
        }

        if(app->feedback_ticks > 0) {
            state.show_feedback = true;
            strncpy(state.sent_message, app->sent_display, sizeof(state.sent_message) - 1);
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
