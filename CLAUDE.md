# GhostMesh — CLAUDE.md

## What This Project Is

GhostMesh is a Flipper Zero companion application that turns the Flipper into a portable offline mesh-radio field terminal. Paired with a Heltec WiFi LoRa 32 V3 running Meshtastic firmware, it enables long-range LoRa mesh communications with no internet, cellular, or phone infrastructure required.

The project has three deliverables:
1. **`flipper-app/`** — A Flipper Application Package (FAP) written in C99
2. **`heltec-firmware/`** — Custom Meshtastic C++ modules for the Heltec backpack (tamper / proximity / arming sensors Meshtastic doesn't provide natively)
3. **`ghostmesh.info/`** — A marketing/documentation website (Astro + Tailwind + GSAP)

The project is thematically tied to the *Tales from the Afternow* audio drama (Sean Kennedy) — Server Monk aesthetic, "Light your candles" tone, GhostMesh as an in-universe WLO-free communications tool. This informs all web copy and documentation tone.

---

## Skills Available

Two project skills are registered and should be used proactively:

- **`/brand_voice_and_content_tone`** — ServerMonk brand voice guide. Use before writing any web copy, documentation, commit messages, or user-facing text to ensure tone consistency.
- **`/ghostmesh-website-access`** — GhostMesh website deploy workflow. Use when building or deploying `ghostmesh.info` to production (IONOS SFTP via lftp).

---

## Hardware Architecture

### Devices

| Device | MCU | Role |
|--------|-----|------|
| Flipper Zero | STM32WB55 (ARM Cortex-M4 + M0+) | Operator handheld terminal |
| Heltec WiFi LoRa 32 V3 | ESP32-S3 (dual-core, 240 MHz) | Radio backpack (Meshtastic) |
| SX1262 | — | 915 MHz LoRa radio (built into Heltec) |

The Heltec backpack is designed to operate fully unattended. The Flipper is a lightweight operator interface only.

### Core Wiring (3 wires)

<pre><code>
Flipper pin 13 (U_TX / USART1 TX)  ──→  Heltec GPIO7  (Serial module RX)
Flipper pin 14 (U_RX / USART1 RX)  ←──  Heltec GPIO6  (Serial module TX)
Flipper GND                  ────  Heltec GND
</code></pre>

**Baud rate:** 115200, 8N1
**Protocol:** Meshtastic Serial module in PROTO mode — PROTO binary framing (StreamAPI)
**Meshtastic config (required):** Module Config → Serial → enabled, mode PROTO, RX 7, TX 6, 115200, override-console OFF

> **Not GPIO43/44.** UART0 (43/44) shares the CP2102 USB bridge, which clamps those pins when the Heltec is on battery — so the old PhoneAPI-on-UART0 link only worked on USB power. GPIO6/7 have no CP2102 and work on pure battery. Confirmed 2026-07-01.

**Power rule:** Never connect Flipper 3.3V or 5V to Heltec. The Flipper's regulator cannot source the 200–500mA the ESP32-S3 draws. Both devices run independent LiPo batteries.

### Sensor Expansion (Phases 7–11)

**On Heltec (backpack):**

| Component | Interface | GPIO / Addr | Phase | Status |
|-----------|-----------|-------------|-------|--------|
| BME280 (temp/humidity/pressure) | I2C bus 2 | 0x76 — GPIO41/42 | 7 | ✅ Meshtastic native |
| BN-220 GPS | UART1 9600 baud | GPIO34 RX / 33 TX | 8 | ✅ Meshtastic native |
| STEMMA QT 5-port passive hub | I2C passthrough | GPIO41/42 | 7 | ✅ |
| SW-520D tilt switch → `TAMPER` | GPIO | GPIO2 | 10 | ✅ `TiltModule` |
| Slide switch (arm/disarm) → `ARMED`/`DISARMED` | GPIO | GPIO4 | 10 | ✅ `ArmingModule` |
| Photoresistor (light tamper) → `TAMPER_LIGHT` | ADC | GPIO5 | 10 | ✅ `LightTamperModule` |
| RCWL-1601 ultrasonic → `PERSON_DETECTED` | GPIO | GPIO38 trig / 47 echo | 11 | ✅ `ProximityModule` — RCWL-1601 at 3.3V (no divider), working on HW |
| IR receiver (NEC remote) → arm/disarm | GPIO | GPIO48 | 10 | ✅ `IRModule` |
| MAX17048 (LiPo fuel gauge) | I2C bus 2 | 0x36 — GPIO41/42 | 9 | 🚧 on-bus, not read (connector mismatch) |
| Passive buzzer via PN2222 → `/buzz` | GPIO (PWM tone) | GPIO39 | 10 | ✅ `CommandModule` — working on HW |
| Vibration motor via PN2222 + 1N4007 → `/vibrate` | GPIO | GPIO40 | 10 | ✅ `CommandModule` — working on HW |
| RGB status LED (SK6812) → `/led` | GPIO (addressable) | GPIO26 | 10 | ✅ `CommandModule` — `neopixelWrite`; colors + green↔red gradient sweep, working on HW |
| Wipe button (tact switch) → factory reset | GPIO (INPUT_PULLUP) | GPIO37 | 10 | ✅ `CommandModule` — wired & working (armed + double-press) |

**On Flipper ProtoBoard (operator controls):**

None. The Flipper carries **no** control hardware — it links to the backpack over just three wires
(TX/RX/GND) and the operator drives everything through the FAP. Every physical control and output
(passive buzzer, vibration motor, RGB LED, arming slide, wipe button) lives on the **backpack**, so
it works standalone and any operator can trigger it over the mesh or IR. The mesh command layer is
`CommandModule` (see `docs/command-cli.md`). The vibration/buzzer drivers use a **PN2222** on the
bench; the EE's PCB uses an **AO3400** MOSFET for the motor (same firmware — a low-side switch is
`HIGH`=on either way).

### I2C Bus Architecture (Heltec)

<pre><code>
Bus 1 — GPIO17 SDA / GPIO18 SCL
  └─ OLED display (0x3C) — hardwired, do not connect externals here

Bus 2 — GPIO41 SDA / GPIO42 SCL
  └─ STEMMA QT 5-port passive hub
      ├─ BME280  (0x76)
      └─ MAX17048 (0x36)
</code></pre>

### Occupied Heltec GPIOs (do not reuse)

- `8–14`: SX1262 LoRa SPI
- `17–18`: I2C bus 1 (OLED)
- `19–20`: USB D-/D+
- `1`: Battery ADC
- `6–7`: Serial module (PROTO) ↔ Flipper — GPIO6 TX, GPIO7 RX
- `2`: SW-520D tilt switch (`TiltModule`)
- `4`: slide switch / arming (`ArmingModule`)
- `5`: photoresistor ADC (`LightTamperModule`)
- `38 / 47`: RCWL-1601 trig / echo — 3.3V, no divider (`ProximityModule`)
- `48`: IR receiver — remote arm/disarm (`IRModule`)
- `41–42`: I2C bus 2
- `43–44`: UART0 / CP2102 USB console — do NOT use for the Flipper link (clamps on battery)
- `39`: passive buzzer via PN2222 driver (`CommandModule` `/buzz`)
- `40`: vibration motor via PN2222 driver + 1N4007 flyback (`CommandModule` `/vibrate`)
- `26`: external RGB status LED — SK6812 data (`CommandModule` `/led`, `neopixelWrite`; working). NOT Vext.
- `37`: wipe button — tact switch, INPUT_PULLUP (`CommandModule` factory reset)
- `21`: OLED reset (hardwired — not free; do not use for the proximity trigger)
- `35`: onboard white LED — `CommandModule` mirrors the `/led` on/off state here (backup indicator alongside the SK6812 on GPIO26)
- `36`: Vext — powers the OLED + external 3.3V rail (software gated, active LOW). GPIO26 is NOT Vext.
- Only free non-strapping header pins on the V3 were `26 / 37 / 39 / 40` — now all four used.

---

## Protocol: PROTO / Meshtastic Serial Module

GhostMesh connects to the Meshtastic **Serial module in PROTO mode** on GPIO7 (RX) / GPIO6 (TX). PROTO mode exposes the same StreamAPI protobuf stream the phone app and Python library use — `want_config`/`config_complete`, `ToRadio`/`FromRadio`. It **requires** config (Module Config → Serial: enabled, PROTO, RX 7, TX 6, 115200, override-console OFF).

> This reverses an earlier design that used the PhoneAPI on UART0 (GPIO43/44) and claimed "SerialModule PROTO doesn't work reliably." That was a misdiagnosis of the CP2102 clamp: the SerialModule had been configured on 43/44, where the USB bridge kills the signal on battery. On free pins 6/7 it works on pure battery. See `docs/wiring.md` and `flipper-app/helpers/proto_notes.md`.

### Frame Format

<pre><code>
[0x94] [0xC3] [len_hi] [len_lo] [protobuf payload]
</code></pre>

The `0x94 0xC3` magic bytes provide RF noise immunity — random LoRa-induced UART noise almost never produces a valid frame header.

### Handshake

1. FAP sends `ToRadio { want_config_id: 42 }`
2. Node replies with ~47 config frames
3. Node sends `FromRadio { config_complete_id: 42 }` — handshake complete
4. FAP sets `connected = true`, title bar changes from `...` to `RDY`, then to the node's battery `%` (or `PWR` on external power) — read from the local node's `NodeInfo` during config. The `want_config` request re-sends every ~2 s until `config_complete` arrives, so a missed request self-heals.

### Key Field Numbers (Meshtastic 2.7.x)

All confirmed by serializing known messages with the meshtastic Python library v2.7.8.

- `to` and `from` in MeshPacket use **fixed32 wire type (5)**, not varint. Wrong wire type silently drops packets.
- `portnum = 1` → `TEXT_MESSAGE_APP`
- `to = 0xFFFFFFFF` → broadcast to all mesh nodes
- Sender displayed as last 4 hex digits of node ID (`from & 0xFFFF`)

See `flipper-app/helpers/proto_notes.md` for the full field reference.

---

## ISR Safety — Critical Constraints

`furi_hal_serial_async_rx_start` fires its callback from the UART interrupt handler, not a thread. The callback chain is:

<pre><code>
UART ISR
  → uart_internal_rx_cb()    (uart_helper.c)
    → on_rx_byte()           (proto_mode.c — byte-level state machine)
      → on_rx_text()         (ghostmesh.c — called on full decoded packet)
</code></pre>

**Rules that must never be broken:**

1. **No mutex acquisition from ISR.** `furi_mutex_acquire` is FreeRTOS-backed and cannot be called from interrupt context. Attempting it silently drops every message with no error indication.
2. **No SD I/O or RTC access from ISR.** All file writes and datetime fetches happen in the main loop only.
3. **No display access from ISR.** ViewPort updates happen in the main loop only.
4. **Signal via `volatile bool` only.** `rx_updated` is declared `volatile` so the compiler does not cache it. This is the correct ISR → main loop signaling primitive on Cortex-M4.

Benign races on multi-byte fields (`rx_text_buf`, `rx_sender`, `rx_rssi`, `rx_snr`) are accepted — a stale display frame is harmless; the next message overwrites everything.

### Main Loop

- **Tick rate:** 200ms (`furi_delay_ms(200)`) — 5 Hz
- **Responsibilities:** Check `rx_updated` flag, copy ISR data, log to CSV, update RX history ring buffer, update display, scroll marquee, clear feedback banner on timeout
- **Input handling** runs on the Flipper input thread (not ISR, not main loop) — direct state modification is safe there

---

## Flipper FAP Source Layout

<pre><code>
flipper-app/
├── application.fam              — FAP metadata (appid, stack size, category)
├── ghostmesh.c                  — App entry, main loop, state machine, ISR callback
├── helpers/
│   ├── proto_mode.c/.h          — PROTO encode/decode, handshake, byte-level RX state machine
│   ├── uart_helper.c/.h         — USART1 init, async RX ISR, TX helper
│   ├── profile_manager.c/.h     — Built-in profiles + SD YAML loader
│   ├── log_manager.c/.h         — SD card CSV append
│   ├── gm_backup.c/.h, sha256.c/.h — encrypted config backup (AES-256-GCM)
│   ├── ir_tx.c/.h               — NEC IR transmit (Control screen)
│   └── proto_notes.md           — Protocol field number reference
└── views/
    ├── main_view.c/.h           — 8-screen menu-hub UI (Profile/Messages/RX history/Sensors/Status/Control/Backup/Settings), marquee, ViewPort draw callback
    └── gm_settings.c/.h         — data-driven descriptor table driving the Settings screen
</code></pre>

### Key Design Decisions

- **Zero third-party libraries.** Protobuf codec is hand-coded; YAML parser is a hand-coded state machine. No nanopb, no libyaml.
- **No dynamic allocation after init.** All heap use is in `ghostmesh_alloc()` only.
- **C99 only.** No C++. GCC `-Werror=format-truncation` enforced — explicit width specifiers on all `snprintf` calls.
- **Comments explain WHY, not WHAT.** Well-named identifiers cover the what.

### Profiles

- 3 hardcoded built-ins (Grid Down, Hiking/SAR, Red Team) + up to 5 from SD card
- SD path: `/ext/apps_data/ghostmesh/profiles.yaml`
- Validation: printable ASCII (0x20–0x7E), max 19 chars for profile name, max 22 chars per message, 12 messages per profile

### Logging

- SD path: `/ext/apps_data/ghostmesh/log_YYYYMMDD.csv`
- Fields: `timestamp, node_id, message, lat, lon, rssi, snr`
- Convert to KML: `python tools/log_to_kml.py log_YYYYMMDD.csv`

---

## Build System

**Tool:** `ufbt` (Micro Flipper Build Tool) — no full firmware clone needed.

<pre><code>
cd flipper-app
ufbt            # build only → dist/ghostmesh.fap
ufbt launch     # build + deploy + run (Flipper USB connected, qFlipper closed)
ufbt clean      # remove build artifacts
ufbt update     # refresh SDK
</code></pre>

**Deploy manually:** Copy `dist/ghostmesh.fap` to `SD:/apps/Tools/ghostmesh.fap`.

---

## Custom Heltec Firmware (`heltec-firmware/`)

Phases 10+ add sensors Meshtastic doesn't support natively. Rather than fork Meshtastic, the
repo vendors just the **custom module source** in `heltec-firmware/`; you drop it into a
Meshtastic firmware checkout at the pinned tag and build. See `heltec-firmware/README.md`.

**Build:** clone `meshtastic/firmware` at tag **`v2.7.15.567b8ea`** (the deployed version), run
`heltec-firmware/setup.sh` (idempotent — copies the modules into `src/modules/`, registers them in
`Modules.cpp`, and applies `gps-timepulse.patch`, which the build won't link without), then
`pio run -e heltec-v3`. Output: `.pio/build/heltec-v3/firmware.factory.bin` — flash at offset `0x0`
(no erase, to keep config). We vendor only the custom modules + patch, not a Meshtastic fork.

**Modules** (each broadcasts a plain-text mesh packet → shows on the Meshtastic app AND the FAP):

| Module | Pin(s) | Broadcasts | Notes |
|--------|--------|-----------|-------|
| `ArmingModule` | GPIO4 (SPDT slide switch) | `ARMED` / `DISARMED` | Sets the shared `ghostmesh_armed` flag |
| `TiltModule` | GPIO2 (SW-520D, ext. pull-down) | `TAMPER` | Replaces the built-in Detection Sensor |
| `LightTamperModule` | GPIO5 (photoresistor ADC) | `TAMPER_LIGHT` | Fires when light rises above ambient |
| `ProximityModule` | GPIO38/47 (RCWL-1601, 3.3V) | `PERSON_DETECTED` | Fires when distance drops below threshold |
| `IRModule` | GPIO48 (VS1838B, NEC) | `ARMED` / `DISARMED` | Remote arm/disarm; sets `ghostmesh_armed` (alongside the slide switch — last action wins). Flipper remote: `flipper-app/GhostMeshBackpack.ir` |
| `CommandModule` | GPIO39/40/26/35/37 outputs | replies (gated) | The *receiving* module: parses `/cmd @target` text — outputs (buzzer/vibration/LED), arm/disarm, wipe, live config (`/set`/`/cfg`), `/put` file upload. `CommandModule_payload.cpp` holds `/put`. |
| `GhostMeshConfig` | — | — | NVS-backed config (~27 settings) every module reads; `/set`/`/cfg` + `ghostmesh_apply_native_config()` (GPS/telemetry). Not a mesh module. |
| `GhostMeshWipe` | — | — | The complete-flash destruct, shared by `CommandModule` + `IRModule`. Not a mesh module. |

> Note: only the top five broadcast plain-text events. `CommandModule`'s replies are individually
> gated by config (`rep_*`/`bc_*`) **and routed to the requester, never broadcast** — a command from
> the web configurator or a wired Flipper is answered off-mesh (`sendToPhone`, zero LoRa airtime); a
> remote node gets a directed unicast. So a reply only rides the mesh when the command came over the
> mesh. (`/cfg` + the `/set` success echo are the always-on control channel; everything else has a
> `rep_*` toggle. Presets: `/arm`//disarm` = SENTINEL, `silent` = BLACKOUT, `mode` = HIBERNATE.)

**Armed gate:** `ArmingModule` reads the slide switch into `volatile bool ghostmesh_armed`
(`GhostMeshArming.h`). The three tamper modules only broadcast when armed — so the backpack can
be handled/staged while DISARMED without spamming the mesh.

**Two hard requirements when running this firmware:**
- **Disable the built-in Detection Sensor** in the Meshtastic app (Module Config → Detection Sensor → OFF) — `TiltModule` owns GPIO2 instead.
- **Use a private channel.** Meshtastic blocks module broadcasts on the default public channel, and both nodes must share a frequency slot (see `docs/meshtastic-setup.md`).

---

## Website (ghostmesh.info)

**Stack:** Astro 6.2.1, Tailwind CSS 4.2.4, GSAP 3.15.0. Node >= 22.12.0 required.

<pre><code>
cd ghostmesh.info
npm run build              # outputs to dist/
bash scripts/deploy.sh     # SFTP mirror to IONOS via lftp
</code></pre>

Deploy credentials live in `parameters.cicd.yaml` (gitignored). Template at `parameters.template.yaml`.

Use the **`/ghostmesh-website-access`** skill for deploy workflows. Use **`/brand_voice_and_content_tone`** before writing any web copy.

**Pages:** index, mission, hardware, software, usecases, roadmap, docs, config (the web configurator — Web Serial `/set`/`/cfg`, esptool-js firmware flasher, `/put` payload upload)
**Animations:** mesh canvas (all pages), scrolling sys-bar, title scramble effects
**Tone:** Afternow universe, Server Monk aesthetic — see brand voice skill

---

## Current Status and Roadmap

**FAP: stable v0.8** — Phases 0–5, 7, 8 complete and merged to `main`. (Phase 6 skipped;
Phase 9 MAX17048 wired but not read.)
**Heltec custom firmware: Phase 10/11 working** (`heltec-firmware/`) — tilt, light, proximity,
the arming gate, IR arm/disarm, and the operator outputs (buzzer, vibration, RGB LED, wipe
button), all over the private mesh; plus `/put` file upload to the node's flash.

Confirmed working on hardware:
- TX/RX text over the mesh; CSV logging (`timestamp,node_id,message,lat,lon,rssi,snr`), marquee, RSSI/SNR, RX history (16)
- 3 built-in + up to 5 SD-loaded custom profiles
- Phase 7: BME280 temp/humidity/pressure on the Sensors screen (long-press Up)
- Phase 8: BN-220 GPS position (lat/lon/alt) on the Sensors screen + lat/lon in the CSV
- Battery %: Heltec battery level in the title bar (…/RDY/%/PWR)
- Phase 10: `TAMPER` (tilt), `TAMPER_LIGHT` (photoresistor), `ARMED`/`DISARMED` (slide switch) — custom Heltec modules, all gated by the arm state; alerts arrive on the FAP as text (RX history/status bar)
- Phase 11: `PERSON_DETECTED` (RCWL-1601 at 3.3V, GPIO38 trig / 47 echo, no divider) — working on HW
- Phase 10 outputs: buzzer (GPIO39), vibration (GPIO40), RGB LED (GPIO26, mirrored on GPIO35), wipe button (GPIO37), and IR arm/disarm (GPIO48) — all working on HW via `CommandModule`/`IRModule`
- Web configurator (`ghostmesh.info/config`): USB Web-Serial config (`/set`/`/cfg`), esptool-js firmware flasher, and `/put` chunked file upload to `/ghostmesh/` on the node (stop-and-wait, CRC32-verified) — working on HW

**Not yet done:** Phase 6 (nuke/stealth/keys), Phase 9 (MAX17048 read), a dedicated FAP
tamper-alert UI (alerts currently show as plain text), env-telemetry CSV columns, wardriving
capture. (IR arm/disarm and the operator buzzer/vibration/LED/wipe outputs are done — see below.)

**Branch strategy:** `main` = stable releases. `phase-N-description` = active development.

**Roadmap:**

| Version | Phase | Feature | Custom Heltec Firmware? |
|---------|-------|---------|------------------------|
| v0.6 | 6 | Nuke button, stealth mode, channel key generation | No |
| v0.7 | 7 | BME280 environmental telemetry | No |
| v0.8 | 8 | GPS + wardriving (BN-220) | No |
| v0.9 | 9 | MAX17048 battery fuel gauge | Yes |
| v1.0 | 10 | Physical controls: buzzer, vibration, tamper detection | Yes — ✅ tamper, arming, buzzer, vibration, RGB LED, IR, wipe button all working on HW; dedicated FAP tamper-alert UI still pending |
| v1.1 | 11 | Dead-drop surveillance (RCWL-1601 proximity) | Yes — ✅ RCWL-1601 at 3.3V, working on HW |
| v1.2 | 12 | SIGINT, jammer detection, wardriving heatmaps | Yes |
| v1.3 | 13 | Remote payload execution (BadUSB, NFC, Sub-GHz relay) | Yes |
| v1.4 | 14 | UART encryption (ChaCha20-Poly1305 AEAD) | Yes |

---

## Documentation

All docs live in `docs/`. Key references:

| File | Contents |
|------|----------|
| `docs/hardware.md` | Full BOM, GPIO allocation, power notes |
| `docs/wiring.md` | Pin tables, safety rules, sensor integration diagrams |
| `docs/developer-guide.md` | Architecture, ISR constraints, state machine, contribution guide |
| `docs/serial-modes.md` | PROTO framing, field numbers, handshake, why not TEXTMSG |
| `docs/roadmap.md` | All phases with feature detail and firmware dependencies |
| `docs/opsec.md` | Encryption layers, nuke button, stealth mode, metadata leakage |
| `docs/user-guide.md` | Screen-by-screen UI walkthrough |
| `flipper-app/helpers/proto_notes.md` | Protobuf field number reference |
| `heltec-firmware/README.md` | Custom Heltec Meshtastic modules — build steps + module list |

