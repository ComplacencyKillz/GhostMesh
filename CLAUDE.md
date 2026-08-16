# GhostMesh — CLAUDE.md

## What This Project Is

GhostMesh is a Flipper Zero companion application that turns the Flipper into a portable offline mesh-radio field terminal. Paired with a Heltec WiFi LoRa 32 V3 running Meshtastic firmware, it enables long-range LoRa mesh communications with no internet, cellular, or phone infrastructure required.

The project has two deliverables:
1. **`flipper-app/`** — A Flipper Application Package (FAP) written in C99
2. **`ghostmesh.info/`** — A marketing/documentation website (Astro + Tailwind + GSAP)

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

```
Flipper pin 13 (U_TX / USART1 TX)  ──→  Heltec GPIO7  (Serial module RX)
Flipper pin 14 (U_RX / USART1 RX)  ←──  Heltec GPIO6  (Serial module TX)
Flipper GND                  ────  Heltec GND
```

**Baud rate:** 115200, 8N1
**Protocol:** Meshtastic Serial module in PROTO mode — PROTO binary framing (StreamAPI)
**Meshtastic config (required):** Module Config → Serial → enabled, mode PROTO, RX 7, TX 6, 115200, override-console OFF

> **Not GPIO43/44.** UART0 (43/44) shares the CP2102 USB bridge, which clamps those pins when the Heltec is on battery — so the old PhoneAPI-on-UART0 link only worked on USB power. GPIO6/7 have no CP2102 and work on pure battery. Confirmed 2026-07-01.

**Power rule:** Never connect Flipper 3.3V or 5V to Heltec. The Flipper's regulator cannot source the 200–500mA the ESP32-S3 draws. Both devices run independent LiPo batteries.

### Planned Sensor Expansion (Phases 7–11)

**On Heltec (unattended backpack):**

| Component | Interface | GPIO / Addr | Phase |
|-----------|-----------|-------------|-------|
| BME280 (temp/humidity/pressure) | I2C bus 2 | 0x76 — GPIO41/42 | 7 |
| MAX17048 (LiPo fuel gauge) | I2C bus 2 | 0x36 — GPIO41/42 | 9 |
| BN-220 GPS | UART1 9600 baud | GPIO34 RX / 33 TX | 8 |
| SW-520D tilt switch | GPIO | GPIO2 | 10 |
| Slide switch (arm/disarm) | GPIO | GPIO4 | 10 |
| Photoresistor (light tamper) | ADC | GPIO5 | 10 |
| IR receiver (NEC remote) | GPIO | GPIO48 | 10 |
| HC-SR04 ultrasonic | GPIO | GPIO38 trig (21 = OLED reset) / 47 echo | 11 |
| STEMMA QT 5-port passive hub | I2C passthrough | GPIO41/42 | 7 |

**On Flipper ProtoBoard (operator controls):**

| Component | Interface | GPIO | Phase |
|-----------|-----------|------|-------|
| Slide switch (arming gate) | GPIO | pin 6 / PB2 | 10 |
| Active buzzer via PN2222 | GPIO | pin 2 / PA7 | 10 |
| Vibration motor via AO3400 + 1N4007 | GPIO | pin 3 / PA6 | 10 |

### I2C Bus Architecture (Heltec)

```
Bus 1 — GPIO17 SDA / GPIO18 SCL
  └─ OLED display (0x3C) — hardwired, do not connect externals here

Bus 2 — GPIO41 SDA / GPIO42 SCL
  └─ STEMMA QT 5-port passive hub
      ├─ BME280  (0x76)
      └─ MAX17048 (0x36)
```

### Occupied Heltec GPIOs (do not reuse)

- `8–14`: SX1262 LoRa SPI
- `17–18`: I2C bus 1 (OLED)
- `19–20`: USB D-/D+
- `1`: Battery ADC
- `6–7`: Serial module (PROTO) ↔ Flipper — GPIO6 TX, GPIO7 RX
- `41–42`: I2C bus 2
- `43–44`: UART0 / CP2102 USB console — do NOT use for the Flipper link (clamps on battery)
- `21`: OLED reset (hardwired — not free; do not use for HC-SR04 trigger)
- `35`: onboard white LED (does NOT work as a UART RX)
- `36`: Vext — powers the OLED + external 3.3V rail (software gated, active LOW). GPIO26 is NOT Vext.

---

## Protocol: PROTO / Meshtastic Serial Module

GhostMesh connects to the Meshtastic **Serial module in PROTO mode** on GPIO7 (RX) / GPIO6 (TX). PROTO mode exposes the same StreamAPI protobuf stream the phone app and Python library use — `want_config`/`config_complete`, `ToRadio`/`FromRadio`. It **requires** config (Module Config → Serial: enabled, PROTO, RX 7, TX 6, 115200, override-console OFF).

> This reverses an earlier design that used the PhoneAPI on UART0 (GPIO43/44) and claimed "SerialModule PROTO doesn't work reliably." That was a misdiagnosis of the CP2102 clamp: the SerialModule had been configured on 43/44, where the USB bridge kills the signal on battery. On free pins 6/7 it works on pure battery. See `docs/wiring.md` and `flipper-app/helpers/proto_notes.md`.

### Frame Format

```
[0x94] [0xC3] [len_hi] [len_lo] [protobuf payload]
```

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

```
UART ISR
  → uart_internal_rx_cb()    (uart_helper.c)
    → on_rx_byte()           (proto_mode.c — byte-level state machine)
      → on_rx_text()         (ghostmesh.c — called on full decoded packet)
```

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

```
flipper-app/
├── application.fam              — FAP metadata (appid, stack size, category)
├── ghostmesh.c                  — App entry, main loop, state machine, ISR callback
├── helpers/
│   ├── proto_mode.c/.h          — PROTO encode/decode, handshake, byte-level RX state machine
│   ├── uart_helper.c/.h         — USART1 init, async RX ISR, TX helper
│   ├── profile_manager.c/.h     — Built-in profiles + SD YAML loader
│   ├── log_manager.c/.h         — SD card CSV append
│   └── proto_notes.md           — Protocol field number reference
└── views/
    └── main_view.c/.h           — Four-screen UI (Profile/Messages/RX history/Sensors), marquee scrolling, ViewPort draw callback
```

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

```bash
cd flipper-app
ufbt            # build only → dist/ghostmesh.fap
ufbt launch     # build + deploy + run (Flipper USB connected, qFlipper closed)
ufbt clean      # remove build artifacts
ufbt update     # refresh SDK
```

**Deploy manually:** Copy `dist/ghostmesh.fap` to `SD:/apps/Tools/ghostmesh.fap`.

---

## Website (ghostmesh.info)

**Stack:** Astro 6.2.1, Tailwind CSS 4.2.4, GSAP 3.15.0. Node >= 22.12.0 required.

```bash
cd ghostmesh.info
npm run build              # outputs to dist/
bash scripts/deploy.sh     # SFTP mirror to IONOS via lftp
```

Deploy credentials live in `parameters.cicd.yaml` (gitignored). Template at `parameters.template.yaml`.

Use the **`/ghostmesh-website-access`** skill for deploy workflows. Use **`/brand_voice_and_content_tone`** before writing any web copy.

**Pages:** index, mission, hardware, software, usecases, roadmap, docs
**Animations:** mesh canvas (all pages), scrolling sys-bar, title scramble effects
**Tone:** Afternow universe, Server Monk aesthetic — see brand voice skill

---

## Current Status and Roadmap

**Stable: v0.8** — Phases 0–5, 7, and 8 complete and merged to `main`. (Phase 6 security
baseline was skipped; Phase 9 MAX17048 is wired but not yet read — see below.)

Confirmed working on hardware (Heltec on battery, no USB tether, 2026-07-01):
- TX: Flipper OK → message appears on second Heltec node via mesh
- RX: Incoming mesh message displayed in GhostMesh status bar
- CSV logging (`timestamp,node_id,message,lat,lon,rssi,snr`), marquee scroll, RSSI/SNR, RX history (16)
- 3 built-in + up to 5 SD-loaded custom profiles
- Phase 7: BME280 temp/humidity/pressure on the Sensors screen (long-press Up)
- Phase 8: BN-220 GPS position (lat/lon/alt) on the Sensors screen + lat/lon in the CSV
- Battery %: Heltec battery level in the title bar (…/RDY/%/PWR) via device_metrics (ADC source)

**Not yet done:** Phase 6 (nuke/stealth/keys), Phase 9 (MAX17048 read — connector mismatch,
parked; battery % uses the Heltec ADC), env-telemetry CSV columns, wardriving capture.

**Branch strategy:** `main` = stable releases. `phase-N-description` = active development.

**Roadmap:**

| Version | Phase | Feature | Custom Heltec Firmware? |
|---------|-------|---------|------------------------|
| v0.6 | 6 | Nuke button, stealth mode, channel key generation | No |
| v0.7 | 7 | BME280 environmental telemetry | No |
| v0.8 | 8 | GPS + wardriving (BN-220) | No |
| v0.9 | 9 | MAX17048 battery fuel gauge | Yes |
| v1.0 | 10 | Physical controls: buzzer, vibration, tamper detection | Yes |
| v1.1 | 11 | Dead-drop surveillance (HC-SR04 proximity) | Yes |
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

---

## Scope and Authorization

GhostMesh is built for authorized security testing, personal lab use, grid-down communications, and open-source learning. It does not implement jamming, spectrum interference, unauthorized traffic capture, or destructive payloads. See `docs/red-team-lab-use-cases.md` for the full authorized use case list.
