#include "main_view.h"

#include <gui/canvas.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ROW_H         10
#define LIST_Y        12

// ── Marquee constants ─────────────────────────────────────────────────────────
//
// TITLE_CHARS: visible chars in the FontPrimary title region (x=2 to x=~99).
//   FontPrimary averages ~9 px/char; 11 chars × 9 px = 99 px, safely before
//   the status label that starts at x=107.
//
// LIST_CHARS / STATUS_CHARS: conservative visible-char estimate for FontSecondary
//   list rows and the status bar.  Erring small ensures scrolling goes far
//   enough to reveal the full string even with wide characters.
//
// SCROLL_SPEED: ticks per character advance (1 tick ≈ 200 ms → ~400 ms/char).
// SCROLL_PAUSE: ticks to hold at the start before the slide begins.

#define TITLE_CHARS   11   // hard clip: prevents title spilling into status label
#define LIST_CHARS    20   // scroll window for list rows — canvas clips the rest naturally
#define STATUS_CHARS  20   // scroll window for status bar — same
#define SCROLL_SPEED   2
#define SCROLL_PAUSE   4

// Short status labels that leave room for the title on the same line.
#define STATUS_RDY  "RDY"
#define STATUS_WAIT "..."
#define STATUS_X    107u   // x position of the status label

struct MainView {
    ViewPort* view_port;
    FuriMutex* mutex;
    MainViewState state;
    MainViewInputCallback input_cb;
    void* input_ctx;
};

// ── Marquee helper ────────────────────────────────────────────────────────────
//
// Returns a pointer into s offset so that the window [ptr, ptr+max_chars)
// slides from the beginning to the end of s.  When the string fits, s is
// returned unchanged.
//
// Cycle: SCROLL_PAUSE ticks at offset 0 → advance one char every SCROLL_SPEED
// ticks until the last max_chars are visible → wrap.

static const char* marquee(const char* s, uint8_t tick, uint8_t max_chars) {
    if(!s || !*s) return "";
    size_t len = strlen(s);
    if(len <= (size_t)max_chars) return s;
    uint8_t over   = (uint8_t)(len - (size_t)max_chars);
    uint8_t period = (uint8_t)(over + SCROLL_PAUSE + 1);
    uint8_t phase  = (uint8_t)((tick / SCROLL_SPEED) % period);
    uint8_t offset = (phase < SCROLL_PAUSE) ? 0 : (uint8_t)(phase - SCROLL_PAUSE);
    return s + offset;
}

// Copy at most max_chars chars from src into dst (dst must be max_chars+1 bytes).
// Always null-terminates.  Used so canvas_draw_str never renders past the clip
// boundary we intend — canvas has no clip-rect primitive.

static void copy_window(char* dst, const char* src, uint8_t max_chars) {
    strncpy(dst, src, max_chars);
    dst[max_chars] = '\0';
}

// ── Profile selection draw ───────────────────────────────────────────────────

static void draw_profile_screen(Canvas* canvas, const MainViewState* s) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "GhostMesh");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, STATUS_X, 9, s->uart_active ? STATUS_RDY : STATUS_WAIT);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    canvas_set_font(canvas, FontSecondary);
    uint8_t visible = (s->profile_count < 4) ? s->profile_count : 4;

    for(uint8_t i = 0; i < visible; i++) {
        uint8_t idx = s->profile_scroll + i;
        if(idx >= s->profile_count) break;

        uint8_t row_y = LIST_Y + (uint8_t)(i * ROW_H);
        bool sel = (idx == s->profile_selected);

        const char* name = marquee(s->profile_names[idx], s->scroll_tick, LIST_CHARS);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_y, 127, ROW_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), name);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), name);
        }
    }

    uint8_t sep_y = LIST_Y + (uint8_t)(visible * ROW_H) + 1;
    canvas_draw_line(canvas, 0, sep_y, 127, sep_y);
    canvas_draw_str(canvas, 2, 63, "OK: Load   BACK: Exit");
}

// ── Message list draw ────────────────────────────────────────────────────────

static void draw_message_screen(Canvas* canvas, const MainViewState* s) {
    // Title: profile name (scrolling, hard-clipped) + compact status label
    char title[TITLE_CHARS + 1];
    copy_window(title, marquee(s->active_profile_name, s->scroll_tick, TITLE_CHARS),
                TITLE_CHARS);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, STATUS_X, 9, s->uart_active ? STATUS_RDY : STATUS_WAIT);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    canvas_set_font(canvas, FontSecondary);
    uint8_t rows = s->visible_rows;

    for(uint8_t i = 0; i < rows; i++) {
        uint8_t idx = s->scroll_offset + i;
        if(idx >= s->message_count) break;

        uint8_t row_y = LIST_Y + (uint8_t)(i * ROW_H);
        bool sel = (idx == s->selected_index);

        const char* msg = marquee(s->messages[idx], s->scroll_tick, LIST_CHARS);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_y, 125, ROW_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), msg);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), msg);
        }
    }

    // Scrollbar
    if(s->message_count > rows) {
        uint8_t track_h = (uint8_t)(rows * ROW_H);
        uint8_t thumb_h = (uint8_t)((track_h * rows) / s->message_count);
        if(thumb_h < 3) thumb_h = 3;
        uint8_t thumb_y = LIST_Y + (uint8_t)((track_h * s->scroll_offset) / s->message_count);
        canvas_draw_line(canvas, 127, LIST_Y, 127, (uint8_t)(LIST_Y + track_h));
        canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
    }

    // Status bar
    uint8_t sep_y = LIST_Y + (uint8_t)(rows * ROW_H) + 1;
    canvas_draw_line(canvas, 0, sep_y, 127, sep_y);

    if(s->show_feedback) {
        char fb[24];
        snprintf(fb, sizeof(fb), "Sent: %.17s", s->sent_message);
        canvas_draw_str(canvas, 2, 63, fb);
    } else if(s->last_rx[0]) {
        canvas_draw_str(canvas, 2, 63, marquee(s->last_rx, s->scroll_tick, STATUS_CHARS));
    } else {
        char hint[32];
        // LOW-3: rx_bytes always 0 in PROTO mode; show TX count + send hint.
        snprintf(hint, sizeof(hint), "TX:%lu  [OK] Send", (unsigned long)s->tx_bytes);
        canvas_draw_str(canvas, 2, 63, hint);
    }
}

// ── RX history draw ──────────────────────────────────────────────────────────

static void draw_rx_history_screen(Canvas* canvas, const MainViewState* s) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "RX History");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, STATUS_X, 9, s->uart_active ? STATUS_RDY : STATUS_WAIT);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    canvas_set_font(canvas, FontSecondary);
    uint8_t rows = s->visible_rows;

    if(s->history_count == 0) {
        canvas_draw_str(canvas, 4, LIST_Y + ROW_H - 2, "No messages yet");
    } else {
        for(uint8_t i = 0; i < rows; i++) {
            uint8_t idx = s->history_scroll + i;
            if(idx >= s->history_count) break;
            uint8_t row_y = LIST_Y + (uint8_t)(i * ROW_H);

            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2),
                            marquee(s->history_lines[idx], s->scroll_tick, LIST_CHARS));
        }

        if(s->history_count > rows) {
            uint8_t track_h = (uint8_t)(rows * ROW_H);
            uint8_t thumb_h = (uint8_t)((track_h * rows) / s->history_count);
            if(thumb_h < 3) thumb_h = 3;
            uint8_t thumb_y = LIST_Y +
                (uint8_t)((track_h * s->history_scroll) / s->history_count);
            canvas_draw_line(canvas, 127, LIST_Y, 127, (uint8_t)(LIST_Y + track_h));
            canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
        }
    }

    uint8_t sep_y = LIST_Y + (uint8_t)(rows * ROW_H) + 1;
    canvas_draw_line(canvas, 0, sep_y, 127, sep_y);
    canvas_draw_str(canvas, 2, 63, "BACK: Return");
}

// ── ViewPort callbacks ───────────────────────────────────────────────────────

static void draw_cb(Canvas* canvas, void* ctx) {
    MainView* mv = ctx;
    furi_mutex_acquire(mv->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(mv->state.screen == GhostMeshScreenProfile) {
        draw_profile_screen(canvas, &mv->state);
    } else if(mv->state.screen == GhostMeshScreenMessages) {
        draw_message_screen(canvas, &mv->state);
    } else {
        draw_rx_history_screen(canvas, &mv->state);
    }
    furi_mutex_release(mv->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    MainView* mv = ctx;
    if(mv->input_cb) {
        mv->input_cb(event->key, event->type, mv->input_ctx);
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

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
