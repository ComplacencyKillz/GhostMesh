#include "main_view.h"

#include <gui/canvas.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MainView {
    ViewPort* view_port;
    FuriMutex* mutex;
    MainViewState state;
    MainViewSendCallback send_cb;
    void* send_ctx;
    MainViewBackCallback back_cb;
    void* back_ctx;
};

static void main_view_draw_cb(Canvas* canvas, void* ctx) {
    MainView* mv = ctx;
    furi_mutex_acquire(mv->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "GhostMesh v0.1");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, mv->state.uart_active ? "UART: ACTIVE" : "UART: ERROR");

    char buf[32];
    snprintf(buf, sizeof(buf), "RX: %lu bytes", (unsigned long)mv->state.rx_bytes);
    canvas_draw_str(canvas, 2, 34, buf);

    snprintf(buf, sizeof(buf), "TX: %lu bytes", (unsigned long)mv->state.tx_bytes);
    canvas_draw_str(canvas, 2, 44, buf);

    canvas_draw_str(canvas, 2, 54, "Mode: TEXTMSG");
    canvas_draw_str(canvas, 2, 64, "[OK] Send  [Back] Exit");

    furi_mutex_release(mv->mutex);
}

static void main_view_input_cb(InputEvent* event, void* ctx) {
    MainView* mv = ctx;
    if(event->type != InputTypePress) return;

    switch(event->key) {
    case InputKeyOk:
        if(mv->send_cb) mv->send_cb(mv->send_ctx);
        break;
    case InputKeyBack:
        if(mv->back_cb) mv->back_cb(mv->back_ctx);
        break;
    default:
        break;
    }
}

MainView* main_view_alloc(void) {
    MainView* mv = malloc(sizeof(MainView));
    memset(mv, 0, sizeof(MainView));

    mv->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    mv->view_port = view_port_alloc();

    view_port_draw_callback_set(mv->view_port, main_view_draw_cb, mv);
    view_port_input_callback_set(mv->view_port, main_view_input_cb, mv);

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

void main_view_set_send_callback(MainView* mv, MainViewSendCallback cb, void* ctx) {
    mv->send_cb = cb;
    mv->send_ctx = ctx;
}

void main_view_set_back_callback(MainView* mv, MainViewBackCallback cb, void* ctx) {
    mv->back_cb = cb;
    mv->back_ctx = ctx;
}
