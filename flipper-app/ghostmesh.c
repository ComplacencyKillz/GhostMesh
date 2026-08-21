#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/text_input.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "helpers/proto_mode.h"
#include "helpers/profile_manager.h"
#include "helpers/log_manager.h"
#include "helpers/ir_tx.h"
#include "helpers/gm_backup.h"
#include "views/main_view.h"

#define TAG             "GhostMesh"
#define VISIBLE_ROWS    4
#define FEEDBACK_TICKS  10   // × 200 ms = 2 s
#define RX_HISTORY_MAX  16

// Menu-hub entries: each maps a label to the screen it opens. The label is what shows in the hub
// list — keep "Control" deliberately plain (don't advertise what it does).
typedef struct {
    const char* name;
    GhostMeshScreen screen;
} MenuEntry;

static const MenuEntry MENU[] = {
    {"Messages",   GhostMeshScreenProfile}, // opens the profile picker → that profile's messages
    {"RX History", GhostMeshScreenRxHistory},
    {"Sensors",    GhostMeshScreenSensors},
    {"Control",    GhostMeshScreenControl},
    {"Status",     GhostMeshScreenStatus},
    {"Settings",   GhostMeshScreenSettings},
    {"Backup",     GhostMeshScreenBackup},
};
#define MENU_COUNT ((uint8_t)(sizeof(MENU) / sizeof(MENU[0])))

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

    // Battery % from device telemetry (latest device_metrics; written from ISR)
    volatile uint8_t rx_battery;    // 0-100, or 101 = powered/external
    volatile bool    battery_valid;

    // Environment telemetry (latest environment_metrics; written from ISR)
    volatile float rx_temp;
    volatile float rx_humidity;
    volatile float rx_pressure;
    volatile bool  env_valid;

    // GPS position (latest Position packet; written from ISR)
    volatile int32_t rx_lat_i;   // deg * 1e7
    volatile int32_t rx_lon_i;
    volatile int32_t rx_alt;     // meters
    volatile bool    pos_valid;

    // RX history ring buffer — newest entry at index 0; only written from main loop
    char rx_history_lines[RX_HISTORY_MAX][84];
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

    // Menu hub state
    uint8_t menu_sel;
    uint8_t menu_scroll;

    // Node armed state, parsed from ARMED/DISARMED mesh text
    bool node_armed;
    bool node_armed_known;

    // Control page state
    uint8_t control_sel;      // 0=Arm 1=Disarm 2=Wipe
    bool    wipe_confirm;     // wipe confirmation prompt active
    uint8_t wipe_confirm_sel; // 0=Cancel 1=Confirm

    // Backup state
    bool request_backup;      // set from input; the main loop runs the modal passphrase + encrypt
    char backup_result[48];

    // Settings state (live node config via /set + /cfg over the local link)
    bool     settings_loaded; // a /cfg reply has populated the values
    uint8_t  settings_sel;    // selected field 0..4
    uint16_t set_prox, set_light;
    bool     set_led, set_buzz, set_vib;

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

// Telemetry callback — also ISR context, store only (see proto_notes.md).
// Only the locally-attached Heltec's metrics drive the title bar / sensor screen;
// other mesh nodes broadcast their own device/env metrics, which we ignore.
static void on_telemetry(const ProtoTelemetry* t, void* ctx) {
    GhostMeshApp* app = ctx;
    if(t->from != proto_mode_get_local_node(app->proto)) return;
    if(t->has_device) {
        app->rx_battery    = t->battery_level;
        app->battery_valid = true;
    }
    if(t->has_env) {
        app->rx_temp     = t->temperature;
        app->rx_humidity = t->humidity;
        app->rx_pressure = t->pressure;
        app->env_valid   = true;
    }
}

// Position callback — also ISR context, store only. Local node only (like telemetry).
static void on_position(const ProtoPosition* p, void* ctx) {
    GhostMeshApp* app = ctx;
    if(p->from != proto_mode_get_local_node(app->proto)) return;
    app->rx_lat_i  = p->latitude_i;
    app->rx_lon_i  = p->longitude_i;
    app->rx_alt    = p->altitude;
    app->pos_valid = true;
}

// ── Settings helpers (local node config over the self-addressed link, no broadcast) ──

static void gm_local_id(GhostMeshApp* app, char* out, size_t sz) {
    uint32_t id = proto_mode_get_local_node(app->proto);
    snprintf(out, sz, "%04lx", (unsigned long)(id & 0xFFFF));
}

// Ask the local node for its current config; the CFG reply repopulates the screen.
static void settings_request(GhostMeshApp* app) {
    app->settings_loaded = false;
    char id[8];
    gm_local_id(app, id, sizeof(id));
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "/cfg @%s", id);
    proto_mode_send_local(app->proto, cmd);
}

// Change the selected field by dir (-1/+1 = down/off, up/on) and push it to the local node.
static void settings_edit(GhostMeshApp* app, int dir) {
    char id[8];
    gm_local_id(app, id, sizeof(id));
    char cmd[40];
    switch(app->settings_sel) {
    case 0: {
        int v = (int)app->set_prox + dir * 25;
        if(v < 20) v = 20;
        if(v > 400) v = 400;
        app->set_prox = (uint16_t)v;
        snprintf(cmd, sizeof(cmd), "/set @%s prox %u", id, (unsigned)v);
        break;
    }
    case 1: {
        int v = (int)app->set_light + dir * 100;
        if(v < 0) v = 0;
        if(v > 4095) v = 4095;
        app->set_light = (uint16_t)v;
        snprintf(cmd, sizeof(cmd), "/set @%s light %u", id, (unsigned)v);
        break;
    }
    case 2:
        app->set_led = (dir > 0);
        snprintf(cmd, sizeof(cmd), "/set @%s led %s", id, app->set_led ? "on" : "off");
        break;
    case 3:
        app->set_buzz = (dir > 0);
        snprintf(cmd, sizeof(cmd), "/set @%s buzz %s", id, app->set_buzz ? "on" : "off");
        break;
    case 4:
        app->set_vib = (dir > 0);
        snprintf(cmd, sizeof(cmd), "/set @%s vib %s", id, app->set_vib ? "on" : "off");
        break;
    default:
        return;
    }
    proto_mode_send_local(app->proto, cmd);
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

    switch(app->screen) {
    case GhostMeshScreenProfile:
        switch(key) {
        case InputKeyUp:
            if(app->profile_sel > 0) {
                app->profile_sel--;
                if(app->profile_sel < app->profile_scroll)
                    app->profile_scroll = app->profile_sel;
            }
            break;
        case InputKeyDown:
            // guard against uint8_t underflow if profile_count == 0
            if(app->profile_count > 0 && app->profile_sel < app->profile_count - 1) {
                app->profile_sel++;
                if(app->profile_sel >= app->profile_scroll + VISIBLE_ROWS)
                    app->profile_scroll = (uint8_t)(app->profile_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk:  // load this profile's messages
            app->msg_sel    = 0;
            app->msg_scroll = 0;
            app->screen     = GhostMeshScreenMessages;
            break;
        case InputKeyBack:  // back to the hub
            app->screen = GhostMeshScreenMenu;
            break;
        default:
            break;
        }
        break;

    case GhostMeshScreenMenu:
        switch(key) {
        case InputKeyUp:
            if(app->menu_sel > 0) {
                app->menu_sel--;
                if(app->menu_sel < app->menu_scroll) app->menu_scroll = app->menu_sel;
            }
            break;
        case InputKeyDown:
            if(app->menu_sel < MENU_COUNT - 1) {
                app->menu_sel++;
                if(app->menu_sel >= app->menu_scroll + VISIBLE_ROWS)
                    app->menu_scroll = (uint8_t)(app->menu_sel - VISIBLE_ROWS + 1);
            }
            break;
        case InputKeyOk:  // open the selected screen (reset its scroll state)
            app->msg_sel           = 0;
            app->msg_scroll        = 0;
            app->rx_history_scroll = 0;
            app->control_sel       = 0;
            app->wipe_confirm      = false;
            app->settings_sel      = 0;
            app->screen            = MENU[app->menu_sel].screen;
            if(app->screen == GhostMeshScreenSettings) {
                settings_request(app); // pull the node's current config into the screen
            }
            if(app->screen == GhostMeshScreenBackup) {
                // The main loop runs the modal passphrase entry + encryption.
                app->request_backup = true;
                strncpy(app->backup_result, "Enter passphrase...",
                        sizeof(app->backup_result) - 1);
                app->backup_result[sizeof(app->backup_result) - 1] = '\0';
            }
            break;
        case InputKeyBack:  // hub is the top level → exit the app
            app->running = false;
            break;
        default:
            break;
        }
        break;

    case GhostMeshScreenMessages: {
        uint8_t msg_count = app->profiles[app->profile_sel].message_count;
        switch(key) {
        case InputKeyUp:
            if(app->msg_sel > 0) {
                app->msg_sel--;
                if(app->msg_sel < app->msg_scroll) app->msg_scroll = app->msg_sel;
            }
            break;
        case InputKeyDown:
            // guard against uint8_t underflow if msg_count == 0
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
        case InputKeyBack:  // back to the profile picker
            app->screen         = GhostMeshScreenProfile;
            app->feedback_ticks = 0;
            break;
        default:
            break;
        }
        break;
    }

    case GhostMeshScreenRxHistory:
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
            app->screen = GhostMeshScreenMenu;
            break;
        default:
            break;
        }
        break;

    case GhostMeshScreenControl:
        if(app->wipe_confirm) {
            switch(key) {
            case InputKeyUp:
            case InputKeyDown:
                // Press only — a held key must not flicker the selection onto CONFIRM.
                if(type == InputTypePress) app->wipe_confirm_sel ^= 1u;
                break;
            case InputKeyOk:
                if(app->wipe_confirm_sel == 1) {
                    // Fire the ARM → WIPE → CONFIRM IR sequence the backpack requires.
                    ghostmesh_ir_send(GHOSTMESH_IR_ARM);
                    furi_delay_ms(200);
                    ghostmesh_ir_send(GHOSTMESH_IR_WIPE);
                    furi_delay_ms(200);
                    ghostmesh_ir_send(GHOSTMESH_IR_CONFIRM);
                    strncpy(app->sent_display, "WIPE via IR", sizeof(app->sent_display) - 1);
                    app->sent_display[sizeof(app->sent_display) - 1] = '\0';
                    app->feedback_ticks = FEEDBACK_TICKS;
                    FURI_LOG_I(TAG, "IR: WIPE sequence sent");
                }
                app->wipe_confirm = false;
                app->wipe_confirm_sel = 0;
                break;
            case InputKeyBack:
                app->wipe_confirm = false;
                app->wipe_confirm_sel = 0;
                break;
            default:
                break;
            }
        } else {
            switch(key) {
            case InputKeyUp:
                if(app->control_sel > 0) app->control_sel--;
                break;
            case InputKeyDown:
                if(app->control_sel < 2) app->control_sel++;
                break;
            case InputKeyOk:
                if(app->control_sel == 0) {
                    ghostmesh_ir_send(GHOSTMESH_IR_ARM);
                    strncpy(app->sent_display, "ARM via IR", sizeof(app->sent_display) - 1);
                    app->sent_display[sizeof(app->sent_display) - 1] = '\0';
                    app->feedback_ticks = FEEDBACK_TICKS;
                } else if(app->control_sel == 1) {
                    ghostmesh_ir_send(GHOSTMESH_IR_DISARM);
                    strncpy(app->sent_display, "DISARM via IR", sizeof(app->sent_display) - 1);
                    app->sent_display[sizeof(app->sent_display) - 1] = '\0';
                    app->feedback_ticks = FEEDBACK_TICKS;
                } else {
                    app->wipe_confirm = true; // open confirmation (defaults to Cancel)
                    app->wipe_confirm_sel = 0;
                }
                break;
            case InputKeyBack:
                app->screen = GhostMeshScreenMenu;
                break;
            default:
                break;
            }
        }
        break;

    case GhostMeshScreenSettings:
        switch(key) {
        case InputKeyUp:
            if(app->settings_sel > 0) app->settings_sel--;
            break;
        case InputKeyDown:
            if(app->settings_sel < 4) app->settings_sel++;
            break;
        case InputKeyLeft:
            if(app->settings_loaded) settings_edit(app, -1);
            break;
        case InputKeyRight:
            if(app->settings_loaded) settings_edit(app, +1);
            break;
        case InputKeyOk:
            settings_request(app); // refresh from the node
            break;
        case InputKeyBack:
            app->screen = GhostMeshScreenMenu;
            break;
        default:
            break;
        }
        break;

    case GhostMeshScreenSensors:
    case GhostMeshScreenStatus:
    case GhostMeshScreenBackup:
    default:
        if(key == InputKeyBack) app->screen = GhostMeshScreenMenu;
        break;
    }
}

// ── Modal passphrase entry ────────────────────────────────────────────────────
// The FAP uses a single fullscreen ViewPort, so to show the Flipper keyboard we swap it out for a
// text_input in a ViewHolder and block the main loop on a semaphore until the operator confirms or
// backs out. Runs on the main thread (never the input/ISR thread).

typedef struct {
    char buf[48];
    bool confirmed;
    FuriSemaphore* done;
} PassEntry;

static void passphrase_ok_cb(void* ctx) {
    PassEntry* pe = ctx;
    pe->confirmed = true;
    furi_semaphore_release(pe->done);
}

static void passphrase_back_cb(void* ctx) {
    PassEntry* pe = ctx;
    pe->confirmed = false;
    furi_semaphore_release(pe->done);
}

// Returns true (and fills out) if the operator entered a passphrase and confirmed.
static bool passphrase_prompt(GhostMeshApp* app, char* out, size_t out_sz) {
    PassEntry pe;
    memset(&pe, 0, sizeof(pe));
    pe.done = furi_semaphore_alloc(1, 0);

    gui_remove_view_port(app->gui, main_view_get_view_port(app->main_view));

    TextInput* ti = text_input_alloc();
    text_input_set_header_text(ti, "Backup passphrase");
    text_input_set_result_callback(ti, passphrase_ok_cb, &pe, pe.buf, sizeof(pe.buf), true);

    ViewHolder* vh = view_holder_alloc();
    view_holder_attach_to_gui(vh, app->gui);
    view_holder_set_back_callback(vh, passphrase_back_cb, &pe);
    view_holder_set_view(vh, text_input_get_view(ti));

    furi_semaphore_acquire(pe.done, FuriWaitForever);

    view_holder_set_view(vh, NULL);
    view_holder_free(vh);
    text_input_free(ti);
    furi_semaphore_free(pe.done);

    gui_add_view_port(app->gui, main_view_get_view_port(app->main_view), GuiLayerFullscreen);

    bool ok = pe.confirmed && pe.buf[0];
    if(ok) {
        strncpy(out, pe.buf, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
    memset(pe.buf, 0, sizeof(pe.buf)); // don't leave the passphrase on the stack
    return ok;
}

// Runs the full backup flow: passphrase → encrypt the captured config → write to SD.
static void run_backup(GhostMeshApp* app) {
    char pass[48];
    if(!passphrase_prompt(app, pass, sizeof(pass))) {
        strncpy(app->backup_result, "Cancelled", sizeof(app->backup_result) - 1);
        app->backup_result[sizeof(app->backup_result) - 1] = '\0';
        return;
    }

    const uint8_t* cfg = NULL;
    uint16_t cfg_len = 0;
    if(!proto_mode_get_config_backup(app->proto, &cfg, &cfg_len)) {
        strncpy(app->backup_result, "No config yet - reconnect", sizeof(app->backup_result) - 1);
    } else {
        char path[96];
        if(gm_backup_write(cfg, cfg_len, pass, proto_mode_get_local_node(app->proto), path,
                           sizeof(path))) {
            const char* base = strrchr(path, '/');
            snprintf(app->backup_result, sizeof(app->backup_result), "Saved %.40s",
                     base ? base + 1 : path);
        } else {
            strncpy(app->backup_result, "Write failed", sizeof(app->backup_result) - 1);
        }
    }
    app->backup_result[sizeof(app->backup_result) - 1] = '\0';
    memset(pass, 0, sizeof(pass));
}

// ── App lifecycle ─────────────────────────────────────────────────────────

static GhostMeshApp* ghostmesh_alloc(void) {
    GhostMeshApp* app = malloc(sizeof(GhostMeshApp));
    memset(app, 0, sizeof(GhostMeshApp));
    app->running = true;
    app->screen  = GhostMeshScreenMenu;  // the hub is home; profile picking lives under Messages

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->proto = proto_mode_alloc(GHOSTMESH_UART_BAUD, on_rx_text, app);
    proto_mode_set_telemetry_callback(app->proto, on_telemetry, app);
    proto_mode_set_position_callback(app->proto, on_position, app);

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

    const char* menu_names[MENU_COUNT];
    for(uint8_t i = 0; i < MENU_COUNT; i++)
        menu_names[i] = MENU[i].name;

    // history_ptrs must outlive each main_view_update call; declared here so
    // the draw callback never reads a dead stack frame.
    const char* history_ptrs[RX_HISTORY_MAX];
    uint8_t scroll_tick = 0;
    uint8_t config_retry_tick = 0;

    MainViewState state = {0};
    state.visible_rows  = VISIBLE_ROWS;
    state.profile_names = profile_names;
    state.profile_count = app->profile_count;
    state.menu_names    = menu_names;
    state.menu_count    = MENU_COUNT;
    state.history_lines = history_ptrs;

    while(app->running) {
        if(app->request_backup) {
            app->request_backup = false;
            run_backup(app); // modal: swaps the viewport for the keyboard, then encrypts to SD
        }
        state.scroll_tick      = scroll_tick++;
        state.screen           = app->screen;
        state.uart_active      = proto_mode_is_connected(app->proto);

        // Handshake self-heal: the initial want_config (sent once at alloc) is
        // lost if the node isn't listening the instant the FAP launches. Re-request
        // every ~2s until connected — mirrors what the Meshtastic phone app does.
        if(state.uart_active) {
            config_retry_tick = 0;
        } else if(++config_retry_tick >= 10) {  // 10 ticks * 200ms = 2s
            proto_mode_request_config(app->proto);
            config_retry_tick = 0;
        }

        state.battery_level    = app->rx_battery;
        state.battery_valid    = app->battery_valid;
        state.env_valid        = app->env_valid;
        state.temperature      = app->rx_temp;
        state.humidity         = app->rx_humidity;
        state.pressure         = app->rx_pressure;
        state.pos_valid        = app->pos_valid;
        state.latitude_i       = app->rx_lat_i;
        state.longitude_i      = app->rx_lon_i;
        state.altitude         = app->rx_alt;
        state.profile_selected = app->profile_sel;
        state.profile_scroll   = app->profile_scroll;
        state.menu_selected    = app->menu_sel;
        state.menu_scroll      = app->menu_scroll;
        state.armed_known      = app->node_armed_known;
        state.armed            = app->node_armed;
        state.control_selected      = app->control_sel;
        state.wipe_confirm          = app->wipe_confirm;
        state.wipe_confirm_selected = app->wipe_confirm_sel;
        state.backup_result         = app->backup_result;
        state.settings_loaded       = app->settings_loaded;
        state.settings_selected     = app->settings_sel;
        state.set_prox              = app->set_prox;
        state.set_light             = app->set_light;
        state.set_led               = app->set_led;
        state.set_buzz              = app->set_buzz;
        state.set_vib               = app->set_vib;

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

            // Status bar: sender + full message text (marquee scrolls it)
            // rx_text_buf is char[64], max strlen 63; %.68s is capped by source.
            snprintf(state.last_rx, sizeof(state.last_rx),
                     "%.7s: %.68s", app->rx_sender, app->rx_text_buf);
            state.last_rx[sizeof(state.last_rx) - 1] = '\0';

            // History entry: full sender + RSSI + full message, no pre-truncation.
            // rssi_str[7] covers all int16_t values ("-32768" = 6 chars + null).
            if(app->rx_history_count < RX_HISTORY_MAX)
                app->rx_history_count++;
            memmove(&app->rx_history_lines[1], &app->rx_history_lines[0],
                    (app->rx_history_count - 1) * sizeof(app->rx_history_lines[0]));
            if(app->rx_rssi != 0) {
                char rssi_str[7];
                snprintf(rssi_str, sizeof(rssi_str), "%d", (int)app->rx_rssi);
                snprintf(app->rx_history_lines[0], sizeof(app->rx_history_lines[0]),
                         "%.7s %sdBm: %.62s", app->rx_sender, rssi_str, app->rx_text_buf);
            } else {
                snprintf(app->rx_history_lines[0], sizeof(app->rx_history_lines[0]),
                         "%.7s: %.73s", app->rx_sender, app->rx_text_buf);
            }

            log_rx_message(app->rx_sender, app->rx_text_buf, app->rx_rssi, app->rx_snr, &dt,
                           app->pos_valid, app->rx_lat_i, app->rx_lon_i);

            // Track the backpack's arm state from its ARMED/DISARMED broadcasts (Status/Control).
            if(strcmp(app->rx_text_buf, "ARMED") == 0) {
                app->node_armed = true;
                app->node_armed_known = true;
            } else if(strcmp(app->rx_text_buf, "DISARMED") == 0) {
                app->node_armed = false;
                app->node_armed_known = true;
            } else if(strncmp(app->rx_text_buf, "CFG ", 4) == 0) {
                // Node's /cfg reply → populate the Settings screen. Parse by key so field order
                // doesn't matter: "CFG prox=150 light=2000 led=1 buzz=1 vib=1".
                const char* t;
                if((t = strstr(app->rx_text_buf, "prox=")))  app->set_prox = (uint16_t)atoi(t + 5);
                if((t = strstr(app->rx_text_buf, "light="))) app->set_light = (uint16_t)atoi(t + 6);
                if((t = strstr(app->rx_text_buf, "led=")))   app->set_led = atoi(t + 4) != 0;
                if((t = strstr(app->rx_text_buf, "buzz=")))  app->set_buzz = atoi(t + 5) != 0;
                if((t = strstr(app->rx_text_buf, "vib=")))   app->set_vib = atoi(t + 4) != 0;
                app->settings_loaded = true;
            }
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
