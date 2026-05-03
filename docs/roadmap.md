# GhostMesh Roadmap

## Phase 0 — Documentation and Sanity Checks ✅

**Goal:** Confirm hardware wiring, document the serial mode landscape, and establish the project foundation.

- [x] Document exact wiring (Flipper pins 13/14 ↔ Heltec RX/TX, shared GND, no shared power)
- [x] Document all Meshtastic serial modes and recommend TEXTMSG for MVP
- [x] Confirm physical UART path alive (byte counters increase via USB-UART bridge)
- [x] Create README, LICENSE, docs structure
- [x] Create repository on GitHub

---

## Phase 1 — UART Byte-Counter FAP (v0.1) ✅

**Goal:** A compilable Flipper app that proves UART communication end-to-end.

- [x] `application.fam` with correct FAP metadata
- [x] `ghostmesh.c` — main entry point, app lifecycle
- [x] `uart_helper.c/.h` — UART init, TX, async RX, cleanup
- [x] `textmsg_mode.c/.h` — TEXTMSG send helper
- [x] `main_view.c/.h` — ViewPort-based display
- [x] Display: GhostMesh v0.1, UART status, RX/TX byte counts, current mode
- [x] OK button sends "CHECKIN OK" test message over UART
- [x] Builds with `ufbt` from `flipper-app/`

**Success criteria:** FAP compiles, deploys, shows UART ACTIVE, and byte counters increment when Meshtastic sends data.

---

## Phase 2 — Canned Message MVP (v0.2)

**Goal:** A navigable menu of canned messages the user can select and send over the mesh.

- [ ] Add `SubmenuView` or custom scroll list for message selection
- [ ] Load canned message list from `examples/canned-messages.json` on SD card (or compile-time fallback)
- [ ] UP/DOWN navigate the list; OK sends the selected message
- [ ] Incoming text from Heltec displayed in a receive/log view (last N lines)
- [ ] Back button returns from receive view to message list
- [ ] Parse incoming TEXTMSG lines and show sender + message if available

**Prerequisite:** Meshtastic serial module must be set to TEXTMSG mode (see `docs/meshtastic-setup.md`).

---

## Phase 3 — Field Profiles (v0.3)

**Goal:** Configurable deployment profiles with context-appropriate message sets.

- [ ] Load profile list from `SD:/apps/Data/ghostmesh/profiles.json`
- [ ] Profile selection screen at startup
- [ ] Each profile contains: name, description, list of canned messages
- [ ] Bundled profiles: Grid-Down, Hiking/SAR, Red-Team Lab
- [ ] Fall back to `examples/field-profiles.json` defaults if SD not present
- [ ] Save last-used profile to persist across restarts

---

## Phase 4 — Logging and Mapping (v0.4)

**Goal:** Capture received message metadata for post-session analysis and KML export.

- [ ] Log received messages to `SD:/apps/Data/ghostmesh/log_YYYYMMDD.csv`
- [ ] CSV fields: timestamp, node_id, message, rssi (if available), snr (if available)
- [ ] `tools/log_to_kml.py` converts CSV with lat/lon fields to KML for Google Earth / QGIS
- [ ] Display last-seen timestamp for known senders in the receive view
- [ ] Optional: integrate Flipper GPS module if hardware is available

---

## Phase 5 — PROTO Mode Client (v0.5+)

**Goal:** Full Meshtastic mesh client using the protobuf serial API.

- [ ] Integrate nanopb + meshtastic protobuf definitions (generated from official protobufs repo)
- [ ] Implement PROTO serial framing: `[0x94 0xC3] [len_hi len_lo] [protobuf payload]`
- [ ] Send `ToRadio` packets (text messages, admin commands, node queries)
- [ ] Receive and parse `FromRadio` packets (text, telemetry, position, node info)
- [ ] Display node list with battery, last-seen, link quality
- [ ] Enable node ping / traceroute display
- [ ] Switch Meshtastic serial mode to `PROTO` (or `DEFAULT`)

See `helpers/proto_notes.md` for implementation notes.

---

## Red-Team Lab Features (Docs Only — Future)

Authorized lab use cases are documented in `docs/red-team-lab-use-cases.md`. Implementation will only proceed after:
- Core GhostMesh comms stack is stable (Phase 2+)
- Features are scoped to owned/authorized systems only
- Local arming and confirmation UI is in place

---

## Versioning

| Version | Phase | Key Feature |
|---------|-------|-------------|
| v0.1 | 1 | UART byte counter, compilable FAP |
| v0.2 | 2 | Canned message menu, TEXTMSG send/receive |
| v0.3 | 3 | Field profiles from SD card |
| v0.4 | 4 | Message logging, KML export |
| v0.5 | 5 | PROTO mode full client |
