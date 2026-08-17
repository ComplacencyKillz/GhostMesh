# GhostMesh Roadmap

## Phase 0 — Documentation and Hardware Validation ✅

- Documented wiring, hardware, serial protocol options
- Confirmed physical UART path alive (byte counters via USB-UART bridge)
- Two-node end-to-end test confirmed (message sent from Python, received on 2f74)

---

## Phase 1 — UART Byte-Counter FAP ✅

Compilable FAP proving the UART communication path.

- `application.fam`, `ghostmesh.c`, `uart_helper.c/.h`
- UART open, RX/TX byte counters, OK sends test message
- Builds with `ufbt`

---

## Phase 2 — Canned Message Selector ✅

Scrollable menu of canned messages replacing the byte-counter display.

- UP/DOWN navigates messages, hold for repeat scroll
- OK sends selected message
- 2-second "Sent:" feedback banner
- Incoming mesh text displayed in status bar
- Scrollbar indicator

---

## Phase 3 — Field Profiles ✅

Profile selection screen before the message list.

- 3 built-in profiles: Grid Down, Hiking / SAR, Red Team
- Profile selector on launch; BACK returns to selector from message list
- SD card YAML loader: up to 5 custom profiles from `profiles.yaml`
- YAML parser with input validation (printable ASCII, length caps, quote stripping)

---

## Phase 4 — Node Logging + KML Export ✅

Capture received message metadata for post-session analysis.

- [x] Log received messages to `SD:/apps_data/ghostmesh/log_YYYYMMDD.csv`
- [x] CSV fields: timestamp, node_id, message, rssi, snr  (lat, lon added in Phase 8)
- [x] `tools/log_to_kml.py` converts CSV with lat/lon fields to KML
- [x] RSSI and SNR decoded from `MeshPacket` (fields 12 and 8) and shown in history screen
- [x] RX history screen (long-press Down): last 16 messages with full sender/RSSI/text
- [x] Marquee scrolling on all text regions that exceed display width
- [ ] Optional: Flipper GPS module integration for lat/lon columns (superseded by Phase 8)

---

## Phase 5 — PROTO Mode Full Client ✅

Full Meshtastic PROTO protocol — hand-coded protobuf encoder/decoder, no external dependencies.

- `proto_mode.c/.h`: want_config_id handshake, config_complete_id detection
- Correct Meshtastic 2.7.x field numbers confirmed from library serialization
- `FromRadio.packet` decoder: extracts sender ID (fixed32) and text payload
- `ToRadio` encoder: correct field 2 fixed32 for `to`, field 9 for `hop_limit`
- RF noise immunity: 0x94 0xC3 framing rejects spurious LoRa-induced UART bytes
- Connects over the Meshtastic Serial module (PROTO mode) on GPIO7/6

**Confirmed working 2026-05-05** (TX/RX over the mesh), **and on battery 2026-07-01** (Serial module on GPIO7/6):
- TX: Flipper OK → message appears on second Heltec node ✓
- RX: message from second node → displayed in GhostMesh status bar ✓
- Runs on the Heltec's own battery with no USB tether (moved off UART0/43-44 — the CP2102 clamps those unless USB-powered) ✓

---

## Phase 6 — Security Baseline ⏳

Software-only phase. No new hardware required. Establishes the burn-proof protocol.

### 6.1 Nuke Button
Send `AdminMessage { factory_reset: true }` via the existing PROTO (Serial module) connection.
Meshtastic processes this natively — wipes all AES channel keys and reboots. No custom
Heltec firmware needed.

- [ ] New protobuf encode path in `proto_mode.c` for AdminMessage
- [ ] Dedicated Nuke screen in FAP (long-press OK + confirm, or dedicated key combo)
- [ ] Gate: nuke only executes if slide switch pin reads HIGH (implement check now even before hardware arrives — pin reads LOW by default, safe)
- [ ] Single red LED blink on the Heltec's onboard LED confirms wipe from a distance

### 6.2 Private Channel Key Generation
Use Flipper's hardware RNG to generate a cryptographically secure 256-bit channel key and
configure it on the Heltec without ever typing it manually.

- [ ] `furi_hal_random_fill_buf(key, 32)` → Base64 encode
- [ ] Send `AdminMessage { set_channel }` with generated key via PROTO
- [ ] Display key fingerprint (first 8 chars) for out-of-band verification with squad

### 6.3 Stealth Mode
Single FAP toggle sends a sequence of config packets to minimize the node's RF footprint.

- [ ] Set device role to ROUTER (node relays but does not initiate NodeInfo beacons)
- [ ] Disable GPS position broadcasting
- [ ] Disable device metrics telemetry beaconing
- [ ] Disable Heltec OLED and status LEDs (node goes dark)
- [ ] Stealth indicator in FAP title bar

### 6.4 Private Channel Setup Documentation
- [ ] `docs/opsec.md`: default channel warning (AQ== key = public), channel setup workflow,
  recommended device role configurations for different operational scenarios

---

## Phase 7 — Environmental Telemetry ✅ (telemetry display shipped; env CSV + ducting pending)

**New hardware:** BME280 (I2C) + STEMMA QT 5-port passive hub

Wire BME280 to Heltec I2C bus 2 (SDA=GPIO41, SCL=GPIO42) via the Adafruit hub. Enable
"Environment Telemetry" in Meshtastic Module Config. No custom Heltec firmware needed —
BME280 support is built into Meshtastic 2.7.x.

- [x] Hardware: solder Qwiic cables BME280 → hub → Heltec GPIO41/42 header pins
- [x] FAP: decode `Telemetry` FromRadio packet type (in `proto_mode.c`)
  - Fields: temperature (float), relative_humidity (float), barometric_pressure (float)
- [x] Display: dedicated Sensor screen (long-press Up → Temp/Humid/Press + GPS line)
- [ ] CSV log: add temp_c, humidity_pct, pressure_hpa columns (CSV currently logs lat/lon only)
- [ ] RF ducting prediction: if humidity > 80% and ΔP trending positive → "DUCTING" indicator
  in status bar (exceptional LoRa propagation conditions)

---

## Phase 8 — GPS ✅ / Wardriving ⏳ (GPS position + logging shipped; wardriving deferred)

**New hardware:** BN-220 GPS module

Wire BN-220 to Heltec UART1 (GPIO34=RX, GPIO33=TX — **confirmed working**; the original
35/36 are wrong: GPIO35 is the onboard LED and won't receive UART, GPIO36 is Vext and
powers the OLED). Power the GPS from the always-on 3.3V rail for bring-up. Enable GPS in
Meshtastic Position config (Receive GPIO=34, Transmit GPIO=33). No custom Heltec firmware needed.

**GPS power note:** The BN-220 draws 20–40mA continuously. For battery savings you can later
gate it via Vext (GPIO36, active LOW) — but Vext also powers the OLED, so toggle it
deliberately. For bring-up, power from the always-on 3.3V rail. Cold-start fix time: 30–90
seconds (needs sky view); hot-start ~1 second.

- [x] Hardware: BN-220 TX (white) → GPIO34, RX (green) → GPIO33, VCC (red) → 3.3V, GND (black) → GND
- [x] Meshtastic config: GPS ENABLED, Receive GPIO=34, Transmit GPIO=33, 9600 baud (auto-detected)
- [x] FAP: decode `Position` FromRadio packet (lat/lon/alt) → shown on Sensor screen
- [x] FAP: decode `NodeInfo` FromRadio packet (local node battery %; remote-node filter in place)
- [x] CSV: lat/lon columns populated → `log_to_kml.py` gets real positions (alt not yet logged)
- [ ] Wardriving mode: dedicated FAP screen, captures NodeInfo from all reachable nodes
  (default channel), logs [timestamp, node_id, user, lat, lon, rssi, snr] to separate
  `wardrive_YYYYMMDD.csv`; KML export shows signal heatmap

---

## Phase 9 — Battery Intelligence ⏳ (MAX17048 wired but not read — parked; battery % via ADC)

**New hardware:** Adafruit MAX17048 fuel gauge (I2C, Qwiic)

Wire MAX17048 to the same Qwiic hub as BME280. I2C address 0x36 — no conflict with
BME280 (0x76) or OLED (0x3C on bus 1). Requires a small custom Meshtastic module
(one .cpp file) that reads SOC from MAX17048 and injects it into the existing
`device_metrics.battery_level` field in the telemetry stream.

- [ ] Hardware: MAX17048 Qwiic → hub → GPIO41/42; JST-PH 2-pin → Heltec battery connector
- [ ] Custom Meshtastic module: `MAX17048Module.cpp` reads I2C SOC %, writes to device_metrics
- [x] FAP: parse battery_level from device_metrics telemetry → shown in title bar
- [x] UI: persistent battery % indicator in title bar (…/RDY/%/PWR)
- [x] Fallback: standard Meshtastic ADC battery level (this is what the FAP shows today; the
  MAX17048 is physically wired but not read — JST connector mismatch vs the Heltec cell, parked)

---

## Phase 10 — Physical Controls & Alerting ⏳

The backpack must operate fully unattended. All tamper and proximity sensors live on the
Heltec and are handled by custom Meshtastic modules — they broadcast alerts over LoRa
autonomously with no Flipper present. The Flipper ProtoBoard carries only the
**operator-facing** controls: the arming gate and alert feedback.

### Heltec (backpack — unattended operation)

**New hardware:** SW-520D tilt switch (GPIO2), slide switch arm/disarm (GPIO4),
photoresistor (GPIO5 ADC), IR receiver (GPIO48). Requires custom Meshtastic modules.

| Heltec GPIO | Component | Behavior |
|-------------|-----------|----------|
| 2 | SW-520D tilt switch | Trigger → broadcast TAMPER over LoRa; if armed → nuke |
| 4 | Slide switch | Physical arm/disarm on deployment |
| 5 (ADC) | Photoresistor | Below threshold → broadcast TAMPER_LIGHT over LoRa |
| 48 | IR receiver (NEC) | Decoded remote code → arm/disarm backpack from ~10m |

- [x] Tilt → TAMPER broadcast: custom **`TiltModule`** (GPIO2, arm-gated) — replaced the built-in Detection Sensor (disable it). Working 2026-08-17. (The built-in also works standalone/ungated.)
- [ ] Custom module (only for the armed-nuke path): tilt GPIO2 → `AdminMessage` factory-reset when the arming gate is set
- [x] Custom module: photoresistor ADC polling → threshold crossing → TAMPER_LIGHT mesh packet — **`heltec-firmware/LightTamperModule`, working 2026-08-16**
- [ ] Custom module: IR receiver NEC decode on GPIO48 → arm/disarm state toggle, confirm via OLED
- [x] Custom module: slide switch GPIO4 → **`ArmingModule`** sets `ghostmesh_armed` on boot + toggle and broadcasts `ARMED`/`DISARMED`; the tilt/light/proximity modules only alert when armed. Working 2026-08-17.

### Flipper ProtoBoard (operator — carried in the field)

**New hardware:** slide switch (pin 6), active buzzer via PN2222 (pin 2), vibration
motor via AO3400 + 1N4007 (pin 3). FAP changes only — no Heltec firmware needed.

| Flipper Pin | Component | Behavior |
|-------------|-----------|----------|
| 6 (PB2) | Slide switch | Arming gate — nuke and destructive actions only fire when HIGH |
| 2 (PA7) | Buzzer via PN2222 | Audible alert on incoming message or relayed tamper event |
| 3 (PA6) | Vibration motor via AO3400 + 1N4007 | Haptic alert on incoming message |

- [ ] FAP: read slide switch (pin 6) as arming gate — gate on all destructive actions
- [ ] FAP: drive buzzer (pin 2) on RX message received, tamper alert received, send confirmation
- [ ] FAP: drive vibration motor (pin 3) on RX message and send confirmation
- [ ] FAP: parse incoming TAMPER / TAMPER_LIGHT / PERSON_DETECTED mesh packets and trigger alerts

---

## Phase 11 — Dead-Drop Surveillance ⏳

**New hardware:** HC-SR04 ultrasonic sensor (Heltec GPIO38 trigger — NOT 21, which is the OLED reset; GPIO47 echo)

**Voltage note (confirmed 2026-08-16):** the plain blue HC-SR04 does NOT work at 3.3V — it
returns garbage (0 cm). It needs **5V**, with a divider on Echo (1kΩ → GPIO47, 2kΩ → GND) to
drop the 5V echo to 3.3V. That works on the bench (USB), but the battery backpack has no 5V,
so deployment needs a **3.3V-native module (RCWL-1601 / JSN-SR04T)** — a drop-in, no firmware
change (RCWL-1601 ordered). The EE's 3.3V HC-SR04 on the schematic needs this correction.

- [x] Hardware: Trigger → GPIO38, Echo → GPIO47 (5V + divider on Echo for the plain HC-SR04)
- [x] Custom Heltec module: `heltec-firmware/ProximityModule` — poll HC-SR04 at 1Hz, threshold
  200cm, broadcast `PERSON_DETECTED` text packet over LoRa. Working on bench 2026-08-16.
- [ ] Enhancement — operator-adjustable sensor thresholds (firmware + FAP): custom modules
  accept a config command (e.g. `PROX=150`, `LIGHT=1800`), apply live, and persist to flash;
  a GhostMesh FAP settings screen sends it. No reflash to re-tune light/proximity thresholds.
- [ ] FAP: receive `PERSON_DETECTED` packet from mesh; log to CSV with timestamp;
  trigger buzzer + vibration; if armed → initiate nuke
- [ ] FAP: dead-drop arm/disarm from IR remote (Phase 10 IR infra)
- [ ] CSV log: add person_detected_at column
- [ ] Unattended mode: FAP enters low-power display-off state, only wakes on a proximity mesh packet

---

## Phase 12 — SIGINT & Advanced Sensing ⏳

Leverages all installed hardware for intelligence gathering.

### 12.1 Jammer Detection
- [ ] Custom Heltec module: continuously read SX1262 RSSI register; detect sustained noise
  floor spike without valid LoRa preambles → broadcast `JAMMER_DETECTED` mesh packet
- [ ] FAP: display `SIGNAL INTERFERENCE` alert; log to CSV with timestamp and RSSI reading

### 12.2 RF Ducting Display
- [ ] Phase 7 BME280 data feeds a live ducting likelihood score (humidity + ΔP algorithm)
- [ ] FAP: persistent indicator when ducting conditions favor exceptional propagation

### 12.3 Advanced Wardriving
- [ ] Wardriving mode captures NodeInfo from all channels the node can hear
- [ ] KML export shows color-coded signal strength heatmap per node
- [ ] Optional: Heltec Vext (GPIO36, active LOW — also powers the OLED) power cycles the GPS to force position refresh on demand

---

## Phase 13 — Remote Payload Execution ⏳

Transforms GhostMesh from a communications tool into a remote hardware orchestrator.
All features require slide switch in the ARMED position before execution.

### 13.1 BadUSB over Mesh
- [ ] FAP listens for `PAYLOAD_n` mesh packet (on private channel only)
- [ ] On receipt with switch armed: invoke Flipper BadUSB service with pre-staged
  DuckyScript `.txt` from SD card `SD:/apps_data/ghostmesh/payloads/`
- [ ] Confirm execution via mesh ACK packet

### 13.2 NFC Orchestration over Mesh
- [ ] `NFC_EMU:filename.nfc` packet → FAP loads NFC file and starts emulation
  (Flipper taped to reader, triggered remotely from miles away)
- [ ] `NFC_HARVEST` packet → FAP reads badge UID and broadcasts `NFC_UID:xxxxxxxx`
  back over mesh
- [ ] NFC coil experiment: wind copper wire from Elegoo stepper motor coil as extended
  antenna (experimental — not tuned for 13.56MHz, results vary)

### 13.3 Sub-GHz Relay
- [ ] Flipper A captures 433MHz fixed-code signal
- [ ] Size gate: only relay if raw capture ≤ 200 bytes (LoRa max payload ~255 bytes;
  header overhead ~50 bytes). Reject and warn user if too large.
- [ ] Encapsulate into Meshtastic text packet with `SUBGHZ:` prefix + hex-encoded payload
- [ ] Flipper B receives, strips prefix, replays 433MHz signal locally
- [ ] Limitation: rolling codes, frequency-hopping, and long-preamble signals will not fit

---

## Phase 14 — UART Encryption ⏳

Last phase — touches everything. Full authenticated encryption of the Flipper ↔ Heltec
UART link using ChaCha20-Poly1305 (AEAD: encrypts + authenticates each frame).

### Why ChaCha20-Poly1305 (not XOR, not AES-CBC)
- XOR with static key: trivially broken with one known-plaintext pair. Not worth implementing.
- AES-CBC: no authentication, vulnerable to padding oracle, more code.
- ChaCha20-Poly1305: fast on Cortex-M4 without hardware AES acceleration, authenticated
  (detects tampering and corruption), well-audited, fits in ~2KB of code.

### Key Management
- [ ] Key generated on Flipper via `furi_hal_random_fill_buf()` (32 bytes)
- [ ] Key stored in Flipper secure storage (encrypted flash region)
- [ ] One-time provisioning: before encryption is enabled, Flipper sends key to Heltec
  over the plaintext UART; after confirmation, both sides enable encryption simultaneously
- [ ] Key rotation: new key generated, exchanged, confirmed, old key wiped

### Implementation
- [ ] FAP: ChaCha20-Poly1305 encrypt every UART TX frame; decrypt + authenticate every RX
- [ ] Heltec custom firmware: decrypt incoming frames → pass to Meshtastic's internal StreamAPI;
  encrypt outgoing StreamAPI frames → send over the Serial module UART (GPIO6/7)
- [ ] Frame format: `[nonce 12B][ciphertext][poly1305 tag 16B]`
- [ ] Counter-based nonce: 64-bit monotonic counter prevents nonce reuse across reboots

---

## Architecture Notes

### UART Architecture (Heltec)
- Serial module PROTO (GPIO7 RX / GPIO6 TX): **Flipper connection** — StreamAPI PROTO frames
- UART0 (GPIO43/44): CP2102 USB console — flashing/debug only (clamps on battery; not the Flipper link)
- UART1 (GPIO34 RX / GPIO33 TX): GPS — NMEA input from BN-220

### I2C Bus Architecture (Heltec)
- Bus 1 (GPIO17/18): OLED display — hardwired to board, do not attach hub here
- Bus 2 (GPIO41/42): Sensor bus — BME280 (0x76) + MAX17048 (0x36) via STEMMA QT hub

### UART Communication Architecture (Flipper ↔ Heltec)
- The only Flipper↔Heltec link is the Meshtastic Serial module in PROTO mode on GPIO6/7
  (ToRadio / FromRadio protobuf, `0x94 0xC3` framing).
- Heltec sensor events (tamper, proximity, jammer, etc.) are **broadcast as LoRa mesh packets**
  by their custom modules — typically a short text message (e.g. `TAMPER`). They reach the
  Flipper as ordinary `FromRadio` PROTO frames, so the FAP's existing decoder handles them.
  Broadcasting over the mesh (not the wire) is what lets a deployed backpack alert the
  operator when the Flipper isn't attached.

### Design Principle

The **Heltec backpack operates fully unattended**. All sensors that need to function
without the Flipper present live on the Heltec and are driven by custom Meshtastic modules.
The Flipper ProtoBoard carries only operator-facing controls (arming gate, haptic/audio
feedback). The Flipper is not required for the backpack to detect intrusion, send alerts
over LoRa, or protect itself.

### What Requires Custom Heltec Firmware

| Feature | Stock Meshtastic | Requires Custom Module |
|---------|-----------------|----------------------|
| BME280 telemetry | ✅ built-in | — |
| BN-220 GPS | ✅ built-in | — |
| Private channel config | ✅ via PROTO AdminMessage | — |
| Factory reset (nuke) | ✅ AdminMessage | — |
| Disable beaconing | ✅ via config | — |
| Tilt switch tamper alert | ❌ | Custom module |
| Photoresistor tamper alert | ❌ | Custom module |
| IR receiver arm/disarm | ❌ | Custom module |
| HC-SR04 proximity alert | ❌ | ✅ ProximityModule (built) |
| MAX17048 accurate SOC | ❌ | Custom module |
| Jammer detection | ❌ | Custom module |
| UART encryption | ❌ | Full custom firmware layer |

Slide switch (operator gate), buzzer, and vibration motor are on the **Flipper ProtoBoard**
and require only FAP changes — no Heltec firmware.

---

## Versioning

| Version | Phase | Key Feature |
|---------|-------|-------------|
| v0.1 | 1 | UART byte counter, compilable FAP |
| v0.2 | 2 | Canned message menu, TEXTMSG send/receive |
| v0.3 | 3 | Field profiles, SD card YAML loader |
| v0.4 | 4 | Message logging, KML export, RSSI/SNR decode |
| v0.5 | 5 | PROTO mode full client — ✅ done |
| v0.6 | 6 | Security baseline, nuke button, stealth mode — ⏭ skipped |
| v0.7 | 7 | Environmental telemetry (BME280) — ✅ done |
| v0.8 | 8 | GPS + wardriving — ✅ GPS done, wardriving deferred · **current stable** |
| v0.9 | 9 | Battery intelligence (MAX17048) — ⏳ wired, not read (parked) |
| v1.0 | 10 | Physical controls, alerting, tamper detection |
| v1.1 | 11 | Dead-drop surveillance (HC-SR04) |
| v1.2 | 12 | SIGINT, jammer detection, advanced wardriving |
| v1.3 | 13 | Remote payload execution (BadUSB, NFC, Sub-GHz relay) |
| v1.4 | 14 | UART encryption (ChaCha20-Poly1305) |
