# GhostMesh Developer Guide

## Architecture Overview

GhostMesh is a Flipper Zero FAP (Flipper Application Package) written in C. It connects
to a Heltec ESP32-S3 running Meshtastic over UART using Meshtastic's binary PROTO protocol.

~~~
ghostmesh.c          — app entry point, main loop, state machine, modal passphrase entry
helpers/
  proto_mode.c/.h    — PROTO encode/decode, handshake, rx state machine, config capture
  uart_helper.c/.h   — USART1 init, async RX (ISR-driven), TX
  profile_manager.c/.h — built-in profiles, SD card YAML loader
  log_manager.c/.h   — SD card CSV append
  ir_tx.c/.h         — transmit the GhostMesh NECext IR command set
  gm_backup.c/.h     — AES-256-GCM encrypted config backup → SD
  sha256.c/.h        — bundled SHA-256 (the backup KDF)
views/
  main_view.c/.h     — menu-hub UI (8 hub screens), shared chrome, marquee, ViewPort callbacks
  views/gm_settings.c/.h — data-driven descriptor table driving the Settings screen
~~~

---

## Build System

GhostMesh uses **ufbt** (Micro Flipper Build Tool). No full Flipper firmware clone needed.

```bash
cd flipper-app
ufbt          # build
ufbt launch   # build + deploy + run (Flipper connected via USB, qFlipper closed)
ufbt clean    # clean artifacts
ufbt update   # update SDK
~~~

The FAP targets the official Flipper Zero SDK. `application.fam` declares the entry point,
stack size, and category. API compatibility is checked at build time (`APPCHK`).

---

## Key Constraints

### UART RX runs in ISR context

`furi_hal_serial_async_rx_start` fires its callback from the UART interrupt handler —
not a thread. The callback chain is:

~~~
uart_internal_rx_cb (uart_helper.c)
  → on_rx_byte (proto_mode.c)       — byte-level PROTO state machine
    → on_rx_text (ghostmesh.c)      — called when a full text packet is decoded
~~~

**Consequences:**
- Never call `furi_mutex_acquire` from `on_rx_text` or anything it calls. FuriMutex is
  backed by FreeRTOS mutexes which cannot be taken from ISR. Doing so silently drops every
  received message. See `proto_notes.md`.
- ISR-written fields in `GhostMeshApp` (`rx_sender`, `rx_text_buf`, `rx_rssi`, `rx_snr`,
  `rx_updated`) use `volatile` for the flag. Reading and processing happens in the main loop.
- All SD card I/O, RTC access, and display updates happen in the main loop only.

### Main loop tick rate

The main loop runs at 200ms (`furi_delay_ms(200)`). All state transitions, sensor reads,
and view updates happen within this budget. The marquee scroll tick increments once per
loop iteration.

---

## State Machine

`GhostMeshApp` holds all app state. The main loop reads it, builds a `MainViewState`
snapshot, and calls `main_view_update()` each tick.

### Screens

```c
typedef enum {
    GhostMeshScreenProfile,    // message-set picker (reached via Menu → Messages)
    GhostMeshScreenMenu,       // hub / home
    GhostMeshScreenMessages,   // canned message list
    GhostMeshScreenRxHistory,  // last 16 received
    GhostMeshScreenSensors,    // telemetry + GPS
    GhostMeshScreenStatus,     // node state overview
    GhostMeshScreenControl,    // IR arm / disarm / wipe
    GhostMeshScreenBackup,     // encrypted config backup
    GhostMeshScreenSettings,   // live node config (/set + /cfg over the local link)
} GhostMeshScreen;
~~~

Add new screens by extending this enum, adding draw logic to `main_view.c`, handling input in
`on_input` (`ghostmesh.c`), and — if it's a hub destination — adding a `MENU[]` entry.

### Input handling

`on_input` runs on the Flipper's input thread (not ISR, not main loop). It modifies app state
directly via a per-screen switch. Navigation keys fire on `InputTypePress`, `InputTypeRepeat`,
and `InputTypeLong`; action keys on `InputTypePress` only.

The hub is home: it opens the selected screen, and every screen's BACK returns to it. The Backup
entry sets a `request_backup` flag that the **main loop** consumes to run the modal passphrase
prompt — a `text_input` in a `view_holder`, swapped in for the main ViewPort and blocked on a
semaphore until the operator confirms.

---

## PROTO Protocol

GhostMesh hand-codes all protobuf encoding and decoding. No nanopb or other library.

### Sending

~~~
proto_mode_send_text(proto, "CHECKIN OK")
  → proto_encode_text()        builds ToRadio { packet: MeshPacket { ... } }
  → uart_helper_send_bytes()   writes framed packet to UART
~~~

PROTO framing: `0x94 0xC3 [len_hi] [len_lo] [protobuf payload]`

Sends are gated on `proto->connected` — the handshake must complete first.
`want_config_id: 42` is sent on startup; `config_complete_id: 42` in a FromRadio
packet sets `connected = true`.

### Receiving

A byte-level state machine in `on_rx_byte` (proto_mode.c) synchronizes on the
`0x94 0xC3` magic bytes, reads the 2-byte length, accumulates the payload, then
decodes the FromRadio protobuf. `TEXT_MESSAGE_APP` packets surface via the `ProtoRxCallback`;
`TELEMETRY_APP` (67) and `POSITION_APP` (3) surface via the optional `ProtoTelemetryCallback`
and `ProtoPositionCallback`.

### Adding a new FromRadio packet type

1. Add field decoding in `on_rx_byte`'s FromRadio dispatch block in `proto_mode.c`
2. Extend `ProtoRxCallback` or add a separate callback type in `proto_mode.h` for the
   new data (e.g., `ProtoTelemetryCallback`, `ProtoPositionCallback`)
3. Wire the new callback in `ghostmesh_alloc()` in `ghostmesh.c`
4. Handle the data in the main loop

Confirmed field numbers for all current and planned packet types are in
`helpers/proto_notes.md` and `docs/serial-modes.md`.

### Adding a new ToRadio message type

1. Add an encode function in `proto_mode.c` following the pattern of `proto_encode_text()`
2. Add a public API declaration in `proto_mode.h`
3. Call from `ghostmesh.c` as needed

---

## UI System

### ViewPort + Mutex

`MainView` wraps a Flipper `ViewPort`. The draw callback runs on the GUI thread.
`main_view_update()` acquires the mutex, copies the `MainViewState` struct, releases,
then triggers a redraw. The draw callback reads from the copied state under the same mutex.

### Adding a new screen

1. Add a value to `GhostMeshScreen` in `main_view.h`
2. Add a draw function `draw_X_screen(Canvas*, const MainViewState*)` in `main_view.c`
3. Dispatch to it in `draw_cb()`
4. Add fields to `MainViewState` if the screen needs new data
5. Handle navigation to/from the screen in `on_input()` in `ghostmesh.c`
6. Populate the new state fields in the main loop

### Marquee scrolling

`marquee(s, tick, max_chars)` in `main_view.c` returns a pointer into `s` offset so
that the visible window slides from start to end over time. `max_chars` is the estimated
number of characters that fit in the display region — it determines how far the marquee
travels. For hard-clipped regions (the title bar), use `copy_window()` after `marquee()`
to prevent overflow into adjacent elements. For full-width regions (list rows, status bar),
draw the marquee pointer directly and let the canvas clip at x=127.

`scroll_tick` in `MainViewState` increments once per main loop tick (200ms). All text on
all screens scrolls in sync.

---

## SD Card Logging

`log_manager.c` opens `SD:/apps_data/ghostmesh/log_YYYYMMDD.csv`, writes a header on
first creation, then appends one CSV row per call. It is called from the **main loop**
(never from ISR) after reading `rx_updated`. The RTC datetime is fetched in the main loop
and passed to `log_rx_message()` to avoid ISR-unsafe RTC calls.

---

## Profile Loading

`profile_manager.c` handles both built-in and SD card profiles.

- `profile_load_builtins()` fills the first 3 slots with hardcoded profiles.
- `profile_load_yaml()` parses `SD:/apps_data/ghostmesh/profiles.yaml` line by line.
  The parser is a simple state machine: `name:` lines start a profile, `- text` lines
  add messages. Input is validated (printable ASCII, length caps, quote stripping).
  Storage for SD-loaded message strings lives in `GhostMeshApp.sd_buf` on the heap.

---

## Phase Development Conventions

Each phase gets its own branch: `phase-N-short-description`. All changes for that phase
land on the branch before merging to main. The roadmap (`docs/roadmap.md`) tracks what
each phase covers and what requires custom Meshtastic firmware vs. FAP-only changes.

Phases 10+ introduce custom Meshtastic modules on the Heltec. Their source lives in the
`heltec-firmware/` directory in this repo (module `.cpp/.h`, plus `gps-timepulse.patch` and
`setup.sh`). To build, clone the Meshtastic firmware at the pinned tag, run
`heltec-firmware/setup.sh` (copies the modules in, registers them in `Modules.cpp`, and applies
the GPS patch — the build won't link without it), then `pio run -e heltec-v3`. See
`heltec-firmware/README.md` for the steps.

### The Heltec module system

Meshtastic modules extend `MeshModule` (C++) and register themselves at startup. A module
can:
- Listen for incoming mesh packets and react
- Read local hardware (I2C sensors, GPIOs) on a timer
- Broadcast mesh packets autonomously

Sensor events (tamper, proximity, jammer, etc.) are broadcast as ordinary **mesh packets**
over LoRa — typically a short text message such as `TAMPER`. They reach the Flipper as
normal `FromRadio` PROTO frames on the existing GPIO6/7 link, so the FAP's PROTO decoder
already handles them; there is no separate serial "sentinel" protocol. Broadcasting over the
mesh (rather than the wire) is what lets a deployed backpack alert the operator when the
Flipper is nowhere near it.

**GhostMesh's Heltec modules** live in `heltec-firmware/` (run `setup.sh` against a Meshtastic
checkout at tag `v2.7.15.567b8ea`, then `pio run -e heltec-v3`):

- `ArmingModule` (GPIO4) — toggle switch; any flip inverts `volatile bool ghostmesh_armed` (`GhostMeshArming.h`) and broadcasts `ARMED`/`DISARMED`
- `TiltModule` (GPIO2) → `TAMPER` — **replaces the built-in Detection Sensor (disable it in the app)**
- `LightTamperModule` (GPIO5 ADC) → `TAMPER_LIGHT`
- `ProximityModule` (GPIO38/47) → `PERSON_DETECTED`
- `IRModule` (GPIO48) — NECext decode (addr `0x474D`); arm / disarm + the `ARM→WIPE→CONFIRM` destruct
- `CommandModule` — **listens** for `/cmd @target` mesh text; drives buzzer/vibration/LED, status, arm/disarm, wipe, live config (`/set`/`/cfg`), and `/put` file upload (the first *receiving* module)
- `CommandModule_payload.cpp` — the `/put` chunked-file receiver (base64 over PROTO → LittleFS, CRC32-verified), a split-out part of `CommandModule`
- `GhostMeshConfig` — the NVS-backed config layer (~23 settings) read by every module; `/set`/`/cfg` and `ghostmesh_apply_native_config()`
- `GhostMeshWipe` — the complete-flash destruct, shared by `CommandModule` and `IRModule`

The tamper modules check `ghostmesh_armed` and only broadcast when armed. Alerts are plain
`TEXT_MESSAGE_APP` packets, so they need a **private channel** (blocked on the default), and both
nodes must share a frequency slot.

---

## Coding Style

- C99, no C++
- No dynamic allocation after init (heap used only in `ghostmesh_alloc`)
- No comments explaining what code does — names do that. Comments only for non-obvious
  WHY: ISR constraints, hardware quirks, protocol gotchas
- No error handling for conditions that cannot occur in practice
- All ISR-shared variables: `volatile` for the synchronization flag; accept benign races
  on multi-byte fields (display strings)
- GCC `-Werror=format-truncation` is active — use explicit width specifiers on all
  `snprintf` calls with variable-length arguments

---

## Adding a Dependency

GhostMesh has no third-party dependencies and should stay that way. The protobuf codec is
hand-coded, the YAML parser is hand-coded, and the backup crypto uses a bundled SHA-256
(`sha256.c`) plus the Flipper's `furi_hal_crypto` AES-256-GCM — no external library. (Phase 14
UART encryption will add a single-header ChaCha20-Poly1305 in the same spirit.) Adding a library
requires a strong justification.
