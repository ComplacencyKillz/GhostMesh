#pragma once

#include <gui/view_port.h>
#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

// Snapshot of app state passed to the view for rendering.
// Copied by value so the draw callback never reads live mutable state.
typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    bool uart_active;
} MainViewState;

typedef struct MainView MainView;

// Fired on the GUI thread when the user presses OK.
typedef void (*MainViewSendCallback)(void* context);

// Fired on the GUI thread when the user presses BACK.
typedef void (*MainViewBackCallback)(void* context);

MainView* main_view_alloc(void);
void main_view_free(MainView* view);

// Returns the ViewPort to register with the Gui service.
ViewPort* main_view_get_view_port(MainView* view);

// Copy state into the view and schedule a redraw.
// Safe to call from the main app thread.
void main_view_update(MainView* view, const MainViewState* state);

void main_view_set_send_callback(MainView* view, MainViewSendCallback cb, void* context);
void main_view_set_back_callback(MainView* view, MainViewBackCallback cb, void* context);
