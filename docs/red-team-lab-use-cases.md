# Red-Team Lab Use Cases

## Scope Statement

This document describes **authorized, lab-scoped** use cases for GhostMesh in security testing contexts. All features described here apply only to:

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

**Use case:** During an authorized assessment where cellular and WiFi comms may be monitored or unavailable, use GhostMesh as a fallback coordination channel.

**How it works:**
- Each team member carries a Flipper Zero + Heltec node pair
- Canned messages cover common field status updates: check-in, moving, hold, RTB, etc.
- Messages travel over encrypted Meshtastic LoRa mesh — no cell towers, no WiFi, no internet

**Implementation status:** Available in Phase 2 (canned messages)

**Safety boundary:** Used for team coordination only. No target system interaction over this channel.

---

## 2. Lab-Only Remote Action Trigger Framework

**Use case:** Demonstrate a lab proof-of-concept for remote trigger delivery over LoRa mesh during an authorized red-team exercise.

**Design constraints (non-negotiable):**

- Requires **explicit local arming** on the Flipper — no trigger fires without a two-step confirm on the physical device
- Uses an **allowlisted command string set only** — no arbitrary command execution
- **No malware, no credential theft, no exfiltration, no destructive payloads**
- All actions must be **benign and reversible** in lab context:
  - Blink a connected LED
  - Write a timestamped log entry to a file
  - Play an audible beep on a lab device
  - Trigger a harmless demo script (e.g., print "TRIGGERED" to a terminal)
- **Lab nodes only** — the receiving device must be pre-configured with GhostMesh listener software
- All trigger messages use a shared PSK or session token negotiated in-person before the exercise

**Implementation status:** Placeholder/docs only. Will require Phase 5 (PROTO mode) for reliable delivery with ACK.

**Why this matters for authorized testing:** LoRa mesh as a C2 transport is a realistic out-of-band channel that bypasses network monitoring. Demonstrating this in a lab allows defenders to understand detection gaps and develop countermeasures.

---

## 3. Quiet Field Diagnostics Mode

**Use case:** Reduce visual signature during a field exercise by dimming or turning off the Flipper display while still logging mesh traffic.

**Features planned:**
- Toggle display off / low brightness (if supported by Flipper firmware)
- Continue logging incoming UART/mesh text messages to SD card
- LED indicators only for critical events (incoming message, node contact)
- Silent mode: no vibration, no audio

**What this is NOT:**
- Not an eavesdropping tool — only logs messages on your own authorized mesh channel
- Does not capture RF traffic outside of the configured Meshtastic mesh
- Does not implement radio promiscuous mode or packet sniffing

**Implementation status:** Planned for Phase 4 (logging). Display control in Phase 3+.

---

## 4. Dead-Drop Health Monitor

**Use case:** During an authorized engagement or lab exercise, monitor the health of pre-deployed Meshtastic nodes (nodes you own and placed as part of the engagement).

**Features planned:**
- Periodic ping/query to known node IDs
- Display last-seen timestamp, battery level (if available via telemetry), signal strength
- Alert if a node goes silent for N minutes (possible discovery/compromise indicator)
- Log node health over time to SD card

**What this is NOT:**
- Not a tool for querying nodes you do not own
- Does not enumerate or interact with third-party Meshtastic networks
- Node IDs must be pre-configured in the device (no automated discovery of foreign nodes)

**Implementation status:** Planned for Phase 5 (requires PROTO mode for telemetry access).

---

## Contributing Red-Team Use Cases

If you have an authorized use case to add:

1. Describe the scenario in terms of a real penetration testing workflow
2. Define explicit safety constraints (what it will never do)
3. Identify what Meshtastic protocol features it requires
4. Submit a PR with a new section in this document and a corresponding issue

Use cases that involve unauthorized access, destructive actions, or non-lab targets will not be accepted.
