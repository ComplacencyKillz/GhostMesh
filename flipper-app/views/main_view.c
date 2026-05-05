#include "main_view.h"

#include <gui/canvas.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ROW_H    10
#define LIST_Y   12

struct MainView {
    ViewPort* view_port;
    FuriMutex* mutex;
    MainViewState state;
    MainViewInputCallback input_cb;
    void* input_ctx;
};

static void draw_cb(Canvas* canvas, void* ctx) {
    MainView* mv = ctx;
    furi_mutex_acquire(mv->mutex, FuriWaitForever);
    const MainViewState* s = &mv->state;

    canvas_clear(canvas);

    // ── Title bar ────────────────────────────────────────────────────
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "GhostMesh");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 88, 9, s->uart_active ? "UART:OK" : "UART:ERR");
    canvas_draw_line(canvas, 0, 11, 127, 11);

    // ── Message list ─────────────────────────────────────────────────
    canvas_set_font(canvas, FontSecondary);
    uint8_t rows = s->visible_rows;

    for(uint8_t i = 0; i < rows; i++) {
        uint8_t idx = s->scroll_offset + i;
        if(idx >= s->message_count) break;

        uint8_t row_y = LIST_Y + (uint8_t)(i * ROW_H);
        bool selected = (idx == s->selected_index);

        if(selected) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_y, 125, ROW_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), s->messages[idx]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), s->messages[idx]);
        }
    }

    // ── Scrollbar ────────────────────────────────────────────────────
    if(s->message_count > rows) {
        uint8_t track_h = (uint8_t)(rows * ROW_H);
        uint8_t thumb_h = (uint8_t)((track_h * rows) / s->message_count);
        if(thumb_h < 3) thumb_h = 3;
        uint8_t thumb_y = LIST_Y +
            (uint8_t)((track_h * s->scroll_offset) / s->message_count);
        canvas_draw_line(canvas, 127, LIST_Y, 127, (uint8_t)(LIST_Y + track_h));
        canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
    }

    // ── Separator + status bar ───────────────────────────────────────
    uint8_t sep_y = LIST_Y + (uint8_t)(rows * ROW_H) + 1;
    canvas_draw_line(canvas, 0, sep_y, 127, sep_y);

    char status[48];
    if(s->show_feedback) {
        snprintf(status, sizeof(status), "Sent: %.22s", s->sent_message);
    } else if(s->last_rx[0]) {
        snprintf(status, sizeof(status), "RX: %.34s", s->last_rx);
    } else {
        snprintf(status, sizeof(status), "TX:%lu  RX:%lu",
                 (unsigned long)s->tx_bytes,
                 (unsigned long)s->rx_bytes);
    }
    canvas_draw_str(canvas, 2, 63, status);

    furi_mutex_release(mv->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    MainView* mv = ctx;
    if(mv->input_cb) {
        mv->input_cb(event->key, event->type, mv->input_ctx);
    }
}

MainView* main_view_alloc(void) {
    MainView* mv = malloc(sizeof(MainView));
    memset(mv, 0, sizeof(MainView));
    mv->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    mv->view_port = view_port_alloc();
    view_port_draw_callback_set(mv->view_port, draw_cb, mv);
    view_port_input_callback_set(mv->view_port, input_cb, mv);
    return mv;
}

void main_view_free(MainView* mv) {
    if(!mv) return;
    view_port_free(mv->view_port);
    furi_mutex_free(mv->mutex);
    free(mv);
}

ViewPort* main_view_get_view_port(MainView* mv) {
    return mv->view_port;
}

void main_view_update(MainView* mv, const MainViewState* state) {
    furi_mutex_acquire(mv->mutex, FuriWaitForever);
    mv->state = *state;
    furi_mutex_release(mv->mutex);
    view_port_update(mv->view_port);
}

void main_view_set_input_callback(MainView* mv, MainViewInputCallback cb, void* ctx) {
    mv->input_cb = cb;
    mv->input_ctx = ctx;
}
