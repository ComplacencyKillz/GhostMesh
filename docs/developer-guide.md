---
---
# GhostMesh Developer Guide

## Architecture Overview

GhostMesh is a Flipper Zero FAP (Flipper Application Package) written in C. It connects
to a Heltec ESP32-S3 running Meshtastic over UART using Meshtastic's binary PROTO protocol.

<pre><code>
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
</code></pre>

---

## Build System

GhostMesh uses **ufbt** (Micro Flipper Build Tool). No full Flipper firmware clone needed.

<pre><code>
cd flipper-app
ufbt          # build
ufbt launch   # build + deploy + run (Flipper connected via USB, qFlipper closed)
ufbt clean    # clean artifacts
ufbt update   # update SDK
<pre><code>

The FAP targets the official Flipper Zero SDK. <code>application.fam</code> declares the entry point,
stack size, and category. API compatibility is checked at build time (<code>APPCHK</code>).

---

## Key Constraints

### UART RX runs in ISR context

<code>furi_hal_serial_async_rx_start</code> fires its callback from the UART interrupt handler —
not a thread. The callback chain is:

</code></pre>
uart_internal_rx_cb (uart_helper.c)
  → on_rx_byte (proto_mode.c)       — byte-level PROTO state machine
    → on_rx_text (ghostmesh.c)      — called when a full text packet is decoded
<pre><code>

**Consequences:**
- Never call <code>furi_mutex_acquire</code> from <code>on_rx_text</code> or anything it calls. FuriMutex is
  backed by FreeRTOS mutexes which cannot be taken from ISR. Doing so silently drops every
  received message. See <code>proto_notes.md</code>.
- ISR-written fields in <code>GhostMeshApp</code> (<code>rx_sender</code>, <code>rx_text_buf</code>, <code>rx_rssi</code>, <code>rx_snr</code>,
  <code>rx_updated</code>) use <code>volatile</code> for the flag. Reading and processing happens in the main loop.
- All SD card I/O, RTC access, and display updates happen in the main loop only.

### Main loop tick rate

The main loop runs at 200ms (<code>furi_delay_ms(200)</code>). All state transitions, sensor reads,
and view updates happen within this budget. The marquee scroll tick increments once per
loop iteration.

---

## State Machine

<code>GhostMeshApp</code> holds all app state. The main loop reads it, builds a <code>MainViewState</code>
snapshot, and calls <code>main_view_update()</code> each tick.

### Screens

</code></pre>
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
</code></pre>

Add new screens by extending this enum, adding draw logic to <code>main_view.c</code>, handling input in
<code>on_input</code> (<code>ghostmesh.c</code>), and — if it's a hub destination — adding a <code>MENU[]</code> entry.

### Input handling

<code>on_input</code> runs on the Flipper's input thread (not ISR, not main loop). It modifies app state
directly via a per-screen switch. Navigation keys fire on <code>InputTypePress</code>, <code>InputTypeRepeat</code>,
and <code>InputTypeLong</code>; action keys on <code>InputTypePress</code> only.

The hub is home: it opens the selected screen, and every screen's BACK returns to it. The Backup
entry sets a <code>request_backup</code> flag that the **main loop** consumes to run the modal passphrase
prompt — a <code>text_input</code> in a <code>view_holder</code>, swapped in for the main ViewPort and blocked on a
semaphore until the operator confirms.

---

## PROTO Protocol

GhostMesh hand-codes all protobuf encoding and decoding. No nanopb or other library.

### Sending

<pre><code>
proto_mode_send_text(proto, "CHECKIN OK")
  → proto_encode_text()        builds ToRadio { packet: MeshPacket { ... } }
  → uart_helper_send_bytes()   writes framed packet to UART
</code></pre>

PROTO framing: <code>0x94 0xC3 [len_hi] [len_lo] [protobuf payload]</code>

Sends are gated on <code>proto->connected</code> — the handshake must complete first.
<code>want_config_id: 42</code> is sent on startup; <code>config_complete_id: 42</code> in a FromRadio
packet sets <code>connected = true</code>.

### Receiving

A byte-level state machine in <code>on_rx_byte</code> (proto_mode.c) synchronizes on the
<code>0x94 0xC3</code> magic bytes, reads the 2-byte length, accumulates the payload, then
decodes the FromRadio protobuf. <code>TEXT_MESSAGE_APP</code> packets surface via the <code>ProtoRxCallback</code>;
<code>TELEMETRY_APP</code> (67) and <code>POSITION_APP</code> (3) surface via the optional <code>ProtoTelemetryCallback</code>
and <code>ProtoPositionCallback</code>.

### Adding a new FromRadio packet type

1. Add field decoding in <code>on_rx_byte</code>'s FromRadio dispatch block in <code>proto_mode.c</code>
2. Extend <code>ProtoRxCallback</code> or add a separate callback type in <code>proto_mode.h</code> for the
   new data (e.g., <code>ProtoTelemetryCallback</code>, <code>ProtoPositionCallback</code>)
3. Wire the new callback in <code>ghostmesh_alloc()</code> in <code>ghostmesh.c</code>
4. Handle the data in the main loop

Confirmed field numbers for all current and planned packet types are in
<code>helpers/proto_notes.md</code> and <code>docs/serial-modes.md</code>.

### Adding a new ToRadio message type

1. Add an encode function in <code>proto_mode.c</code> following the pattern of <code>proto_encode_text()</code>
2. Add a public API declaration in <code>proto_mode.h</code>
3. Call from <code>ghostmesh.c</code> as needed

---

## UI System

### ViewPort + Mutex

<code>MainView</code> wraps a Flipper <code>ViewPort</code>. The draw callback runs on the GUI thread.
<code>main_view_update()</code> acquires the mutex, copies the <code>MainViewState</code> struct, releases,
then triggers a redraw. The draw callback reads from the copied state under the same mutex.

### Adding a new screen

1. Add a value to <code>GhostMeshScreen</code> in <code>main_view.h</code>
2. Add a draw function <code>draw_X_screen(Canvas*, const MainViewState*)</code> in <code>main_view.c</code>
3. Dispatch to it in <code>draw_cb()</code>
4. Add fields to <code>MainViewState</code> if the screen needs new data
5. Handle navigation to/from the screen in <code>on_input()</code> in <code>ghostmesh.c</code>
6. Populate the new state fields in the main loop

### Marquee scrolling

<code>marquee(s, tick, max_chars)</code> in <code>main_view.c</code> returns a pointer into <code>s</code> offset so
that the visible window slides from start to end over time. <code>max_chars</code> is the estimated
number of characters that fit in the display region — it determines how far the marquee
travels. For hard-clipped regions (the title bar), use <code>copy_window()</code> after <code>marquee()</code>
to prevent overflow into adjacent elements. For full-width regions (list rows, status bar),
draw the marquee pointer directly and let the canvas clip at x=127.

<code>scroll_tick</code> in <code>MainViewState</code> increments once per main loop tick (200ms). All text on
all screens scrolls in sync.

---

## SD Card Logging

<code>log_manager.c</code> opens <code>SD:/apps_data/ghostmesh/log_YYYYMMDD.csv</code>, writes a header on
first creation, then appends one CSV row per call. It is called from the **main loop**
(never from ISR) after reading <code>rx_updated</code>. The RTC datetime is fetched in the main loop
and passed to <code>log_rx_message()</code> to avoid ISR-unsafe RTC calls.

---

## Profile Loading

<code>profile_manager.c</code> handles both built-in and SD card profiles.

- <code>profile_load_builtins()</code> fills the first 3 slots with hardcoded profiles.
- <code>profile_load_yaml()</code> parses <code>SD:/apps_data/ghostmesh/profiles.yaml</code> line by line.
  The parser is a simple state machine: <code>name:</code> lines start a profile, <code>- text</code> lines
  add messages. Input is validated (printable ASCII, length caps, quote stripping).
  Storage for SD-loaded message strings lives in <code>GhostMeshApp.sd_buf</code> on the heap.

---

## Phase Development Conventions

Each phase gets its own branch: <code>phase-N-short-description</code>. All changes for that phase
land on the branch before merging to main. The roadmap (<code>docs/roadmap.md</code>) tracks what
each phase covers and what requires custom Meshtastic firmware vs. FAP-only changes.

Phases 10+ introduce custom Meshtastic modules on the Heltec. Their source lives in the
<code>heltec-firmware/</code> directory in this repo (module <code>.cpp/.h</code>, plus <code>gps-timepulse.patch</code> and
<code>setup.sh</code>). To build, clone the Meshtastic firmware at the pinned tag, run
<code>heltec-firmware/setup.sh</code> (copies the modules in, registers them in <code>Modules.cpp</code>, and applies
the GPS patch — the build won't link without it), then <code>pio run -e heltec-v3</code>. See
<code>heltec-firmware/README.md</code> for the steps.

### The Heltec module system

Meshtastic modules extend <code>MeshModule</code> (C++) and register themselves at startup. A module
can:
- Listen for incoming mesh packets and react
- Read local hardware (I2C sensors, GPIOs) on a timer
- Broadcast mesh packets autonomously

Sensor events (tamper, proximity, jammer, etc.) are broadcast as ordinary **mesh packets**
over LoRa — typically a short text message such as <code>TAMPER</code>. They reach the Flipper as
normal <code>FromRadio</code> PROTO frames on the existing GPIO6/7 link, so the FAP's PROTO decoder
already handles them; there is no separate serial "sentinel" protocol. Broadcasting over the
mesh (rather than the wire) is what lets a deployed backpack alert the operator when the
Flipper is nowhere near it.

**GhostMesh's Heltec modules** live in <code>heltec-firmware/</code> (run <code>setup.sh</code> against a Meshtastic
checkout at tag <code>v2.7.15.567b8ea</code>, then <code>pio run -e heltec-v3</code>):

- <code>ArmingModule</code> (GPIO4) — toggle switch; any flip inverts <code>volatile bool ghostmesh_armed</code> (<code>GhostMeshArming.h</code>) and broadcasts <code>ARMED</code>/<code>DISARMED</code>
- <code>TiltModule</code> (GPIO2) → <code>TAMPER</code> — **replaces the built-in Detection Sensor (disable it in the app)**
- <code>LightTamperModule</code> (GPIO5 ADC) → <code>TAMPER_LIGHT</code>
- <code>ProximityModule</code> (GPIO38/47) → <code>PERSON_DETECTED</code>
- <code>IRModule</code> (GPIO48) — NECext decode (addr <code>0x474D</code>); arm / disarm + the <code>ARM→WIPE→CONFIRM</code> destruct
- <code>CommandModule</code> — **listens** for <code>/cmd @target</code> mesh text; drives buzzer/vibration/LED, status, arm/disarm, wipe, live config (<code>/set</code>/<code>/cfg</code>), and <code>/put</code> file upload (the first *receiving* module)
- <code>CommandModule_payload.cpp</code> — the <code>/put</code> chunked-file receiver (base64 over PROTO → LittleFS, CRC32-verified), a split-out part of <code>CommandModule</code>
- <code>GhostMeshConfig</code> — the NVS-backed config layer (~23 settings) read by every module; <code>/set</code>/<code>/cfg</code> and <code>ghostmesh_apply_native_config()</code>
- <code>GhostMeshWipe</code> — the complete-flash destruct, shared by <code>CommandModule</code> and <code>IRModule</code>

The tamper modules check <code>ghostmesh_armed</code> and only broadcast when armed. Alerts are plain
<code>TEXT_MESSAGE_APP</code> packets, so they need a **private channel** (blocked on the default), and both
nodes must share a frequency slot.

---

## Coding Style

- C99, no C++
- No dynamic allocation after init (heap used only in <code>ghostmesh_alloc</code>)
- No comments explaining what code does — names do that. Comments only for non-obvious
  WHY: ISR constraints, hardware quirks, protocol gotchas
- No error handling for conditions that cannot occur in practice
- All ISR-shared variables: <code>volatile</code> for the synchronization flag; accept benign races
  on multi-byte fields (display strings)
- GCC <code>-Werror=format-truncation</code> is active — use explicit width specifiers on all
  <code>snprintf</code> calls with variable-length arguments

---

## Adding a Dependency

GhostMesh has no third-party dependencies and should stay that way. The protobuf codec is
hand-coded, the YAML parser is hand-coded, and the backup crypto uses a bundled SHA-256
(<code>sha256.c</code>) plus the Flipper's <code>furi_hal_crypto</code> AES-256-GCM — no external library. (Phase 14
UART encryption will add a single-header ChaCha20-Poly1305 in the same spirit.) Adding a library
requires a strong justification.
