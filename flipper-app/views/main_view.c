#include "main_view.h"

#include <gui/canvas.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ROW_H     10
#define LIST_Y    12
#define VIS_ROWS  4    // list rows that fit between the header (y=11) and footer (y=53)
#define FOOTER_Y  53   // separator line above the global marquee/status bar
#define TEXT_Y    63   // baseline for the footer text

// ── Marquee constants ─────────────────────────────────────────────────────────
// TITLE_CHARS: hard clip for the FontPrimary title (x=2..~99), before the status label at x=104.
// LIST_CHARS / STATUS_CHARS: conservative visible-char window for FontSecondary rows.
// SCROLL_SPEED: ticks per char advance (1 tick ≈ 200 ms). SCROLL_PAUSE: hold before sliding.
#define TITLE_CHARS   11
#define LIST_CHARS    20
#define STATUS_CHARS  20
#define SCROLL_SPEED   2
#define SCROLL_PAUSE   4

#define STATUS_RDY  "RDY"
#define STATUS_WAIT "..."
#define STATUS_X    104u

struct MainView {
    ViewPort* view_port;
    FuriMutex* mutex;
    MainViewState state;
    MainViewInputCallback input_cb;
    void* input_ctx;
};

// ── Marquee helper ────────────────────────────────────────────────────────────
// Returns a pointer into s so the window [ptr, ptr+max_chars) slides start→end. Fits → unchanged.
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

// Copy at most max_chars from src into dst (dst is max_chars+1 bytes). Canvas has no clip-rect,
// so this is how we stop the title spilling into the status label.
static void copy_window(char* dst, const char* src, uint8_t max_chars) {
    strncpy(dst, src, max_chars);
    dst[max_chars] = '\0';
}

// ── Title-bar status label ────────────────────────────────────────────────────
// "..." (connecting) → "RDY" (connected) → "77%" / "PWR" once device_metrics arrives.
static void draw_status_label(Canvas* canvas, const MainViewState* s) {
    char buf[16];
    const char* label;
    if(!s->uart_active) {
        label = STATUS_WAIT;
    } else if(!s->battery_valid) {
        label = STATUS_RDY;
    } else if(s->battery_level > 100) {
        label = "PWR";
    } else {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)s->battery_level);
        label = buf;
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, STATUS_X, 9, label);
}

// ── Shared chrome: header (title + status + rule) and footer (global marquee) ──
// The footer is on EVERY screen: it shows the send-feedback banner, else the scrolling last-RX,
// else a per-screen hint. That keeps the last received message visible from anywhere.
static void draw_header(Canvas* canvas, const char* title, const MainViewState* s) {
    char buf[TITLE_CHARS + 1];
    copy_window(buf, marquee(title, s->scroll_tick, TITLE_CHARS), TITLE_CHARS);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, buf);
    draw_status_label(canvas, s);
    canvas_draw_line(canvas, 0, 11, 127, 11);
}

static void draw_footer(Canvas* canvas, const char* hint, const MainViewState* s) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, FOOTER_Y, 127, FOOTER_Y);
    if(s->show_feedback) {
        char fb[24];
        snprintf(fb, sizeof(fb), "Sent: %.17s", s->sent_message);
        canvas_draw_str(canvas, 2, TEXT_Y, fb);
    } else if(s->last_rx[0]) {
        canvas_draw_str(canvas, 2, TEXT_Y, marquee(s->last_rx, s->scroll_tick, STATUS_CHARS));
    } else if(hint && hint[0]) {
        canvas_draw_str(canvas, 2, TEXT_Y, hint);
    }
}

// ── Generic selectable list (profile picker, menu, message list) ──────────────
static void draw_list(Canvas* canvas, const char** items, uint8_t count, uint8_t selected,
                      uint8_t scroll, bool show_sel, const MainViewState* s) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < VIS_ROWS; i++) {
        uint8_t idx = (uint8_t)(scroll + i);
        if(idx >= count) break;
        uint8_t row_y = (uint8_t)(LIST_Y + i * ROW_H);
        const char* txt = marquee(items[idx], s->scroll_tick, LIST_CHARS);
        if(show_sel && idx == selected) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_y, 125, ROW_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), txt);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), txt);
        }
    }
    if(count > VIS_ROWS) {
        uint8_t track_h = (uint8_t)(VIS_ROWS * ROW_H);
        uint8_t thumb_h = (uint8_t)((track_h * VIS_ROWS) / count);
        if(thumb_h < 3) thumb_h = 3;
        uint8_t thumb_y = (uint8_t)(LIST_Y + (track_h * scroll) / count);
        canvas_draw_line(canvas, 127, LIST_Y, 127, (uint8_t)(LIST_Y + track_h));
        canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
    }
}

// ── Screens ───────────────────────────────────────────────────────────────────

static void draw_profile_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Profiles", s);
    draw_list(canvas, s->profile_names, s->profile_count, s->profile_selected, s->profile_scroll,
              true, s);
    draw_footer(canvas, "OK:Load  BACK:Menu", s);
}

static void draw_menu_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "GhostMesh", s);
    draw_list(canvas, s->menu_names, s->menu_count, s->menu_selected, s->menu_scroll, true, s);
    draw_footer(canvas, "OK:Open  BACK:Exit", s);
}

static void draw_message_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, s->active_profile_name, s);
    draw_list(canvas, s->messages, s->message_count, s->selected_index, s->scroll_offset, true, s);
    char hint[32];
    snprintf(hint, sizeof(hint), "TX:%lu  OK:Send", (unsigned long)s->tx_bytes);
    draw_footer(canvas, hint, s);
}

static void draw_rx_history_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "RX History", s);
    if(s->history_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, LIST_Y + ROW_H - 2, "No messages yet");
    } else {
        draw_list(canvas, s->history_lines, s->history_count, 0, s->history_scroll, false, s);
    }
    draw_footer(canvas, "BACK:Menu", s);
}

static void draw_sensors_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Sensors", s);
    canvas_set_font(canvas, FontSecondary);
    char line[32];
    if(s->env_valid) {
        snprintf(line, sizeof(line), "T:%.1fC  H:%.0f%%", (double)s->temperature,
                 (double)s->humidity);
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), line);
        snprintf(line, sizeof(line), "Press: %.1f hPa", (double)s->pressure);
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 2 * ROW_H - 2), line);
    } else {
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), "Env: no telemetry");
    }
    if(s->pos_valid) {
        snprintf(line, sizeof(line), "GPS %.3f,%.3f", (double)s->latitude_i / 10000000,
                 (double)s->longitude_i / 10000000);
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 3 * ROW_H - 2), line);
        snprintf(line, sizeof(line), "Alt: %ld m", (long)s->altitude);
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 4 * ROW_H - 2), line);
    } else {
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 3 * ROW_H - 2), "GPS: no fix");
    }
    draw_footer(canvas, "BACK:Menu", s);
}

static void draw_status_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Status", s);
    canvas_set_font(canvas, FontSecondary);
    char line[32];

    snprintf(line, sizeof(line), "Link:  %s", s->uart_active ? "connected" : "waiting");
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), line);

    if(!s->battery_valid) {
        snprintf(line, sizeof(line), "Batt:  --");
    } else if(s->battery_level > 100) {
        snprintf(line, sizeof(line), "Batt:  PWR (ext)");
    } else {
        snprintf(line, sizeof(line), "Batt:  %u%%", (unsigned)s->battery_level);
    }
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 2 * ROW_H - 2), line);

    snprintf(line, sizeof(line), "Armed: %s",
             !s->armed_known ? "unknown" : (s->armed ? "ARMED" : "disarmed"));
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 3 * ROW_H - 2), line);

    snprintf(line, sizeof(line), "GPS:   %s", s->pos_valid ? "fix" : "no fix");
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 4 * ROW_H - 2), line);

    draw_footer(canvas, "BACK:Menu", s);
}

// Draws one selectable row (highlight box when sel), used by the Control action list.
static void draw_action_row(Canvas* canvas, uint8_t row_i, const char* txt, bool sel) {
    uint8_t row_y = (uint8_t)(LIST_Y + row_i * ROW_H);
    if(sel) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, row_y, 127, ROW_H);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), txt);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), txt);
    }
}

// Control: IR arm/disarm/wipe. Buttons are labelled here (the *menu* stays discreet). WIPE opens
// an on-screen confirmation before the FAP blasts the ARM→WIPE→CONFIRM IR sequence.
static void draw_control_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Control", s);
    canvas_set_font(canvas, FontSecondary);

    if(s->wipe_confirm) {
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), "WIPE the backpack?");
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 2 * ROW_H - 2), "Erases it completely.");
        draw_action_row(canvas, 2, "Cancel", s->wipe_confirm_selected == 0);
        draw_action_row(canvas, 3, "CONFIRM WIPE", s->wipe_confirm_selected == 1);
        draw_footer(canvas, "Up/Down + OK", s);
        return;
    }

    char line[24];
    snprintf(line, sizeof(line), "Node: %s",
             !s->armed_known ? "unknown" : (s->armed ? "ARMED" : "disarmed"));
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), line);

    static const char* actions[3] = {"Arm", "Disarm", "Wipe"};
    for(uint8_t i = 0; i < 3; i++)
        draw_action_row(canvas, (uint8_t)(i + 1), actions[i], i == s->control_selected);

    draw_footer(canvas, "OK:Send IR", s);
}

// Backup: shows the result of the encrypted config backup (the passphrase is entered on the
// Flipper keyboard, driven modally from the main loop — not on this screen).
static void draw_backup_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Backup", s);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), "Encrypted config -> SD");
    canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + 2 * ROW_H - 2),
                    s->backup_result ? s->backup_result : "");
    draw_footer(canvas, "BACK:Menu", s);
}

// Settings: live node config. Up/Down select a field, Left/Right change it — each change is sent
// to the local node (no broadcast). Values come from the node's /cfg reply.
static void draw_settings_screen(Canvas* canvas, const MainViewState* s) {
    draw_header(canvas, "Settings", s);
    canvas_set_font(canvas, FontSecondary);

    if(!s->settings_loaded) {
        canvas_draw_str(canvas, 4, (uint8_t)(LIST_Y + ROW_H - 2), "Reading node config...");
        draw_footer(canvas, "BACK:Menu", s);
        return;
    }

    char rows[5][24];
    snprintf(rows[0], sizeof(rows[0]), "Proximity  %u cm", (unsigned)s->set_prox);
    snprintf(rows[1], sizeof(rows[1]), "Light thr  %u", (unsigned)s->set_light);
    snprintf(rows[2], sizeof(rows[2]), "LED        %s", s->set_led ? "on" : "off");
    snprintf(rows[3], sizeof(rows[3]), "Buzzer     %s", s->set_buzz ? "on" : "off");
    snprintf(rows[4], sizeof(rows[4]), "Vibration  %s", s->set_vib ? "on" : "off");

    // Five fields, four visible rows — scroll so the selected field stays on screen.
    uint8_t top = (s->settings_selected >= VIS_ROWS) ? (uint8_t)(s->settings_selected - VIS_ROWS + 1) : 0;
    for(uint8_t i = 0; i < VIS_ROWS; i++) {
        uint8_t idx = (uint8_t)(top + i);
        if(idx >= 5) break;
        uint8_t row_y = (uint8_t)(LIST_Y + i * ROW_H);
        bool sel = (idx == s->settings_selected);
        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_y, 127, ROW_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), rows[idx]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 4, (uint8_t)(row_y + ROW_H - 2), rows[idx]);
        }
    }
    draw_footer(canvas, "Up/Dn pick  Lt/Rt set", s);
}

// ── ViewPort callbacks ─────────────────────────────────────────────────────────

static void draw_cb(Canvas* canvas, void* ctx) {
    MainView* mv = ctx;
    furi_mutex_acquire(mv->mutex, FuriWaitForever);
    canvas_clear(canvas);
    switch(mv->state.screen) {
    case GhostMeshScreenProfile:   draw_profile_screen(canvas, &mv->state);    break;
    case GhostMeshScreenMenu:      draw_menu_screen(canvas, &mv->state);       break;
    case GhostMeshScreenMessages:  draw_message_screen(canvas, &mv->state);    break;
    case GhostMeshScreenRxHistory: draw_rx_history_screen(canvas, &mv->state); break;
    case GhostMeshScreenSensors:   draw_sensors_screen(canvas, &mv->state);    break;
    case GhostMeshScreenStatus:    draw_status_screen(canvas, &mv->state);     break;
    case GhostMeshScreenControl:   draw_control_screen(canvas, &mv->state);    break;
    case GhostMeshScreenBackup:    draw_backup_screen(canvas, &mv->state);     break;
    case GhostMeshScreenSettings:  draw_settings_screen(canvas, &mv->state);   break;
    default:                       draw_menu_screen(canvas, &mv->state);       break;
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
