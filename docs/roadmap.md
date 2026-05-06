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

- UP/DOWN navigates 10 messages, hold for repeat scroll
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
- [x] CSV fields: timestamp, node_id, message, rssi, snr
- [x] `tools/log_to_kml.py` converts CSV with lat/lon fields to KML
- [x] RSSI and SNR decoded from `MeshPacket` (fields 12 and 8) and shown in status bar
- [x] Last-seen info in status bar: `sender rssi: message` on each receive
- [ ] Optional: Flipper GPS module integration for lat/lon columns (hardware dependent)

---

## Phase 5 — PROTO Mode Full Client ✅

Full Meshtastic PROTO protocol — hand-coded protobuf encoder/decoder, no external dependencies.

- `proto_mode.c/.h`: want_config_id handshake, config_complete_id detection
- Correct Meshtastic 2.7.x field numbers confirmed from library serialization
- `FromRadio.packet` decoder: extracts sender ID (fixed32) and text payload
- `ToRadio` encoder: correct field 2 fixed32 for `to`, field 9 for `hop_limit`
- RF noise immunity: 0x94 0xC3 framing rejects spurious LoRa-induced UART bytes
- Connects to PhoneAPI on UART0 (GPIO43/44) — no SerialModule dependency

**Confirmed working 2026-05-05:**
- TX: Flipper OK → message appears on second Heltec node ✓
- RX: message from second node → displayed in GhostMesh status bar ✓

---

## Phase 6 — Red Team Lab Features (Future)

Authorized lab use cases documented in `docs/red-team-lab-use-cases.md`.

- Remote action trigger framework (lab-only, benign payloads, local arming required)
- Dead-drop health monitor (node ping, battery status, last-seen via PROTO telemetry)
- Quiet field diagnostics mode (reduced display, SD logging)

These require Phase 4 completion and a stable hardware platform.

---

## Versioning

| Version | Phase | Key Feature |
|---------|-------|-------------|
| v0.1 | 1 | UART byte counter, compilable FAP |
| v0.2 | 2 | Canned message menu, TEXTMSG send/receive |
| v0.3 | 3 | Field profiles, SD card YAML loader |
| v0.4 | 4 | Message logging, KML export, RSSI/SNR decode — **current** |
| v0.5 | 5 | PROTO mode full client |
| v0.6 | 6 | Red team lab features |
