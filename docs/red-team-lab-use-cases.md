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

- Each team member carries a Flipper + Heltec pair
- Canned message profiles cover common field status: check-in, moving, hold, abort, medical
- Messages travel over Meshtastic AES-256 mesh — no cell towers, no WiFi, no internet
- Custom profiles via SD card YAML for operation-specific callouts
- Range: typically 1–10+ km line of sight depending on terrain and antenna

**Private channel required for operational use.** See [docs/opsec.md](opsec.md).

---

## 2. Burn-Proof Protocol (Nuke + Stealth)

**Status: Planned (Phase 6)**

Ensures the device cannot be used against the operator if discovered.

**Nuke button:** A key combination on the Flipper sends `AdminMessage { factory_reset }`
to the Heltec via the existing PROTO link. Meshtastic wipes all channel keys and reboots.
The node becomes an unconfigured device. Gated by a physical slide switch (ARMED position
required) to prevent accidental wipes.

**Stealth mode:** Single toggle in the GhostMesh UI sends config packets to:
- Set device role to ROUTER (node relays but does not announce itself)
- Disable GPS position broadcasting
- Disable device metrics telemetry
- Disable Heltec OLED and status LEDs

**Private channel key generation:** Generate a 256-bit channel key on the Flipper using
hardware RNG and push it to the Heltec — no phone app required in the field.

---

## 3. Dead-Drop Surveillance Node

**Status: Planned (Phase 10–11)**

Deploy a Heltec backpack at a dead-drop location. The node monitors the area and
broadcasts alerts over LoRa to the operator's handheld unit miles away.

**Sensors (all on Heltec, operate without Flipper present):**
- HC-SR04 ultrasonic: person detected within ~2m → broadcasts PERSON_DETECTED over mesh
- SW-520D tilt switch: node disturbed or picked up → broadcasts TAMPER
- Photoresistor: case opened / light detected → broadcasts TAMPER_LIGHT

**Remote arm/disarm:**
- Physical slide switch on the Heltec: set on deployment
- IR receiver on Heltec GPIO48: operator arms/disarms from ~10m using any NEC remote
  or the Flipper's built-in IR transmitter (no need to physically touch the node)

**Operator notification:** The Flipper receives TAMPER / PERSON_DETECTED mesh packets,
triggers the buzzer and vibration motor, and logs to CSV with timestamp.

**Safety constraint:** The nuke can be armed from the dead-drop slide switch. If the
node is discovered and disturbed, it wipes keys before an adversary can extract them.

---

## 4. Environmental Intelligence

**Status: Planned (Phase 7–8)**

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
