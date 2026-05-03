#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <string.h>

#include "helpers/uart_helper.h"
#include "helpers/textmsg_mode.h"
#include "views/main_view.h"

#define TAG "GhostMesh"

// Default test message sent when the user presses OK.
// Phase 2 will replace this with a navigable canned-message menu.
#define TEST_MESSAGE "CHECKIN OK"

typedef struct {
    Gui* gui;
    MainView* main_view;
    UartHelper* uart;

    // Counters written by the UART RX callback and read by the main loop.
    // Volatile is sufficient here — these are display-only counters and a
    // torn read just means a slightly stale number on screen.
    volatile uint32_t rx_bytes;
    volatile uint32_t tx_bytes;

    bool running;
} GhostMeshApp;

// ─── UART callback ──────────────────────────────────────────────────────────
// Called from the UART driver context for each received byte.
// Must return quickly; no blocking calls.

static void on_rx_byte(uint8_t byte, void* ctx) {
    UNUSED(byte);
    GhostMeshApp* app = ctx;
    app->rx_bytes++;
}

// ─── ViewPort callbacks ──────────────────────────────────────────────────────
// These run on the GUI thread and are dispatched by the main view.

static void on_send(void* ctx) {
    GhostMeshApp* app = ctx;
    textmsg_send(app->uart, TEST_MESSAGE);
    // +1 for the newline appended by textmsg_send
    app->tx_bytes += (uint32_t)(strlen(TEST_MESSAGE) + 1);
    FURI_LOG_I(TAG, "Sent: %s", TEST_MESSAGE);
}

static void on_back(void* ctx) {
    GhostMeshApp* app = ctx;
    app->running = false;
}

// ─── App lifecycle ───────────────────────────────────────────────────────────

static GhostMeshApp* ghostmesh_alloc(void) {
    GhostMeshApp* app = malloc(sizeof(GhostMeshApp));
    memset(app, 0, sizeof(GhostMeshApp));
    app->running = true;

    app->uart = uart_helper_alloc(GHOSTMESH_UART_BAUD, on_rx_byte, app);

    app->main_view = main_view_alloc();
    main_view_set_send_callback(app->main_view, on_send, app);
    main_view_set_back_callback(app->main_view, on_back, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, main_view_get_view_port(app->main_view), GuiLayerFullscreen);

    return app;
}

static void ghostmesh_free(GhostMeshApp* app) {
    gui_remove_view_port(app->gui, main_view_get_view_port(app->main_view));
    furi_record_close(RECORD_GUI);

    main_view_free(app->main_view);
    uart_helper_free(app->uart);
    free(app);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

int32_t ghostmesh_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "Starting");

    GhostMeshApp* app = ghostmesh_alloc();

    if(!uart_helper_is_active(app->uart)) {
        FURI_LOG_E(TAG, "Failed to acquire UART — another app may be using it");
    }

    MainViewState state = {0};

    while(app->running) {
        state.rx_bytes = app->rx_bytes;
        state.tx_bytes = app->tx_bytes;
        state.uart_active = uart_helper_is_active(app->uart);
        main_view_update(app->main_view, &state);
        furi_delay_ms(200);
    }

    FURI_LOG_I(TAG, "Exiting");
    ghostmesh_free(app);
    return 0;
}
