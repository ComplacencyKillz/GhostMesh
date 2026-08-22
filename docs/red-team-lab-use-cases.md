# Red-Team Lab Use Cases

## Scope Statement

This document describes **authorized, lab-scoped** use cases for GhostMesh in security
testing contexts. All features described here apply only to:

- Systems you own or have written authorization to test
- Controlled lab environments
- Authorized penetration testing engagements with explicit scope documentation
- Open-source security research and education

**Nothing in this document authorizes or supports:**
- Unauthorized access to third-party devices or networks
- Malware, ransomware, or destructive payloads
- Credential theft, exfiltration, or persistence on non-owned systems
- Unauthorized relay or control of Meshtastic nodes you do not own
- Jamming or interfering with licensed radio spectrum
- Evading detection on systems you are not authorized to test

---

## 1. Out-of-Band Team Coordination

**Status: Available (Phase 2–5)**

During authorized assessments where cellular and WiFi comms may be monitored or
unavailable, GhostMesh provides a fallback coordination channel over LoRa mesh.

- Each operator carries a Flipper + GhostMesh backpack
- Canned message profiles cover common field status: check-in, moving, hold, abort, medical
- Messages travel over Meshtastic AES-256 mesh — no cell towers, no WiFi, no internet
- Custom profiles via SD card YAML for operation-specific callouts
- Range: typically 1–10+ km line of sight depending on terrain and antenna

**Private channel required for operational use.** See [docs/opsec.md](opsec.md).

---

## 2. Burn-Proof Protocol (Destruct + Stealth)

**Status: destruct built (untested on a spare board); output/GPS/telemetry silencing built via the config layer (`silent`, `screen`/`hbled`/`gpsled`, `gps`/`gpsint`/`telint`); one-press stealth (role→ROUTER in a single toggle) + key-gen still Phase 6**

Ensures the device cannot be used against the operator if discovered.

**The destruct:** an armed-gated **complete flash erase** — firmware, config, and channel keys
wiped, the ESP32-S3 left in USB download mode (`heltec-firmware/GhostMeshWipe.cpp`). Fired three
ways, each with its own confirm: a one-time mesh token, an IR `ARM → WIPE → CONFIRM` sequence, or
a physical double-press. It erases the operator's own device only, and is recoverable by reflash +
encrypted config backup. See [docs/opsec.md](opsec.md).

**Stealth mode:** Single toggle in the GhostMesh UI sends config packets to:
- Set device role to ROUTER (node relays but does not announce itself)
- Disable GPS position broadcasting
- Disable device metrics telemetry
- Disable Heltec OLED and status LEDs

**Private channel key generation:** Generate a 256-bit channel key on the Flipper using
hardware RNG and push it to the Heltec — no phone app required in the field.

---

## 3. Dead-Drop Surveillance Node

**Status: tamper + proximity sensors working (Phase 10–11)**

Plant a backpack at a dead-drop. It watches its own perimeter and broadcasts alerts over LoRa to
operators miles away — running unattended, no Flipper present.

**Sensors (custom Heltec modules, arm-gated — they report only when armed):**
- SW-520D tilt: node moved / picked up → broadcasts `TAMPER` (working)
- Photoresistor: case opened / light rises → broadcasts `TAMPER_LIGHT` (working)
- RCWL-1601 ultrasonic: someone within threshold → broadcasts `PERSON_DETECTED` (working, at 3.3 V,
  no divider)

**Arm / disarm — three ways, last action wins:**
- Toggle switch on the backpack (any flip inverts the state; position isn't tied to a state)
- IR from ~10 m — any NEC remote, or the Flipper's Control screen
- A mesh command (`/arm` / `/disarm`)

**Operator notification:** the FAP receives the `TAMPER` / `PERSON_DETECTED` text over the mesh,
shows it, and logs it to CSV. The backpack's own buzzer / vibration can be triggered over the mesh
CLI (`/buzz`, `/vibrate`) or IR — the indicators live on the deployed node, not the Flipper.

**Denial on discovery:** if the node is compromised, the destruct (armed + confirm) burns it to
download mode before an adversary can extract the keys. See use case 2.

---

## 4. Environmental Intelligence

**Status: BME280 telemetry + GPS working; ducting indicator + wardriving planned (Phase 7–8, 12)**

**RF propagation analysis (BME280):** High humidity and temperature inversions create
RF ducting conditions that can extend LoRa range to 100+ miles. GhostMesh will display
a "DUCTING LIKELY" indicator when BME280 data suggests favorable propagation.

**Mesh wardriving (GPS + NodeInfo):** With BN-220 GPS enabled, GhostMesh logs all
received NodeInfo packets (node ID, user, signal strength) with GPS coordinates to a
dated CSV. `tools/log_to_kml.py` converts this to a KML heatmap showing every node
heard and its signal strength relative to your position.

The wardriving schema: `[timestamp, node_id, user, lat, lon, rssi, snr]`

**Use case:** Map the existing Meshtastic mesh density in a target area during pre-op
reconnaissance. Identify relay nodes and coverage gaps.

---

## 5. Remote Payload Execution (Lab Only)

**Status: Planned (Phase 13)**

**BadUSB over mesh:** Deploy a Flipper inside a target workspace (as a "charging device").
When a specific mesh packet arrives on the private channel, the GhostMesh FAP invokes
the Flipper BadUSB service and executes a pre-staged DuckyScript from the SD card.

**Design constraints (non-negotiable):**
- Requires slide switch in ARMED position before any payload can fire
- Scripts are stored on the SD card and selected by name — no arbitrary code injection
- Only fires on the private channel — default channel packets cannot trigger payloads
- All test payloads must be benign and reversible (print to terminal, create a text file,
  blink an LED)
- Lab/owned systems only

**NFC orchestration over mesh:** Send a mesh packet to command the Flipper to:
- Emulate a stored NFC badge (`NFC_EMU:filename.nfc`) — Flipper taped to reader,
  triggered remotely from miles away
- Harvest a badge UID and broadcast it back over mesh (`NFC_HARVEST`)

**Sub-GHz relay:** Capture a 433MHz fixed-code signal on Flipper A, relay it over LoRa
mesh to Flipper B which replays it locally.

Limitation: LoRa max payload is ~255 bytes. Only simple fixed-code signals (some car
fobs, RF outlets) are small enough. Rolling codes and frequency-hopping signals will not fit.

---

## 6. Jammer Detection

**Status: Planned (Phase 12)**

A custom Meshtastic module monitors the SX1262 noise floor continuously. A sustained spike
in background RSSI without valid LoRa preambles indicates active jamming. The module
broadcasts a JAMMER_DETECTED mesh packet to the operator and logs the noise floor reading.

**Use case:** Detect when an adversary is attempting to blind the mesh network in your
operational area.

---

## Contributing Red-Team Use Cases

If you have an authorized use case to add:

1. Describe the scenario in terms of a real penetration testing workflow
2. Define explicit safety constraints (what it will never do)
3. Identify what Meshtastic protocol features it requires
4. Submit a PR with a new section in this document

Use cases involving unauthorized access, destructive actions, or non-lab targets will
not be accepted.
