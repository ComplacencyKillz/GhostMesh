#pragma once

#include <gui/view_port.h>
#include <input/input.h>
#include <furi.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // UART status
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    bool uart_active;

    // Message list (pointer to static array — not copied by value)
    const char* const* messages;
    uint8_t message_count;
    uint8_t visible_rows;
    uint8_t selected_index;
    uint8_t scroll_offset;

    // Last received line from mesh
    char last_rx[48];

    // Send feedback ("Sent: <msg>")
    bool show_feedback;
    char sent_message[24];
} MainViewState;

typedef struct MainView MainView;

// Single input callback — app handles all key logic
typedef void (*MainViewInputCallback)(InputKey key, InputType type, void* context);

MainView* main_view_alloc(void);
void main_view_free(MainView* view);
ViewPort* main_view_get_view_port(MainView* view);
void main_view_update(MainView* view, const MainViewState* state);
void main_view_set_input_callback(MainView* view, MainViewInputCallback cb, void* context);
