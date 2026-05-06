# GhostMesh

**GhostMesh: a Flipper Zero companion interface for offline Meshtastic field communications.**

GhostMesh turns your Flipper Zero into a handheld controller for a Meshtastic-enabled ESP32 LoRa node, enabling encrypted mesh radio messaging without any phone, internet, or cell infrastructure.

---

## What It Does

- Connects the Flipper Zero to a Heltec LoRa node over UART using Meshtastic's full PROTO protocol
- Profile selector on launch — built-in profiles for Grid Down, Hiking/SAR, and Red Team
- Scrollable canned message menu — UP/DOWN to select, OK to send
- Receives incoming mesh text messages and displays them in the status bar
- Upload your own message profiles via a `profiles.yaml` file on the Flipper SD card
- RF noise immune — PROTO framing rejects spurious bytes from the nearby LoRa antenna

---

## Hardware Required

| Component | Details |
|-----------|---------|
| Flipper Zero | With official prototype board |
| ESP32 LoRa node | MakerHawk / Heltec WiFi LoRa 32 V3 compatible |
| Radio | SX1262, 915 MHz antenna |
| Heltec battery | Connected directly to Heltec — not from Flipper |
| Flipper battery | Flipper runs from its own battery |

The Heltec board must be pre-flashed with [Meshtastic firmware](https://meshtastic.org/).

---

## Quick Start

### 1. Wire the hardware

```
Flipper U_TX (pin 13)  →  Heltec GPIO44  (bottom row, pad labeled "RX")
Flipper U_RX (pin 14)  →  Heltec GPIO43  (bottom row, pad labeled "TX")
Flipper GND            →  Heltec GND
NO shared power        —  each device runs from its own battery
```

See [docs/wiring.md](docs/wiring.md) for the full pinout and safety notes.

### 2. No Meshtastic serial module config needed

GhostMesh connects directly to Meshtastic's PhoneAPI on UART0 (the same path as the phone app and Python library over USB). No special serial module settings are required.

Set your region under **Settings → Radio Config → LoRa → Region** and ensure the node is on the default **LongFast** channel. See [docs/meshtastic-setup.md](docs/meshtastic-setup.md).

### 3. Build and install the FAP

```bash
pip install ufbt
cd flipper-app
ufbt
```

Copy `dist/ghostmesh.fap` to `SD:/apps/Tools/` on your Flipper. See [docs/flipper-setup.md](docs/flipper-setup.md).

### 4. Run the app

**Apps → Tools → GhostMesh**

The screen shows `PROTO:...` for a few seconds while the connection handshake completes, then `PROTO:RDY`. Select a profile with UP/DOWN/OK, then navigate the message list and press OK to send.

---

## Custom Profiles via SD Card

Create `SD:/apps_data/ghostmesh/profiles.yaml` on your Flipper:

```yaml
# GhostMesh custom profiles

name: My Profile
- CHECKIN OK
- IN POSITION
- ABORT
- EXFIL NOW

name: Another Profile
- MESSAGE ONE
- MESSAGE TWO
```

Up to 5 custom profiles are loaded alongside the 3 built-ins. See `examples/profiles.yaml` for the full documented template.

---

## Project Structure

```
ghostmesh/
├── README.md
├── docs/
│   ├── hardware.md           Hardware specs
│   ├── wiring.md             Exact pinout and GPIO conflict notes
│   ├── meshtastic-setup.md   Meshtastic config (minimal — no serial module needed)
│   ├── flipper-setup.md      ufbt build and install
│   ├── serial-modes.md       PROTO protocol field numbers and implementation notes
│   ├── roadmap.md            Phased development plan
│   └── red-team-lab-use-cases.md  Authorized lab use cases (docs only)
├── flipper-app/
│   ├── application.fam
│   ├── ghostmesh.c           App entry point, profile/message/screen state
│   ├── helpers/
│   │   ├── proto_mode.h/.c   PROTO encoder/decoder, handshake, UART state machine
│   │   ├── profile_manager.h/.c  Built-in profiles + YAML loader
│   │   ├── uart_helper.h/.c  USART1 init and async RX/TX
│   │   └── proto_notes.md    Protocol implementation reference
│   └── views/
│       └── main_view.h/.c    Two-screen UI (profile list + message list)
├── examples/
│   └── profiles.yaml         Documented YAML template for custom profiles
├── tests/
│   ├── uart-test-plan.md     Manual hardware validation checklist
│   └── proto_send_test.py    Python PROTO test script (bypasses Flipper)
└── tools/
    └── log_to_kml.py         Phase 4 scaffold: CSV → KML node log export
```

---

## Protocol

GhostMesh uses Meshtastic's binary PROTO protocol with a `0x94 0xC3` framing header. The full connection handshake (~47 config frames) completes in a few seconds on startup. All protobuf field numbers were confirmed against the meshtastic Python library (v2.7.8) — see [docs/serial-modes.md](docs/serial-modes.md) for the complete reference.

---

## Safety and Scope

This project is for **authorized security work, personal lab testing, grid-down comms experimentation, and open-source learning only.**

No malware, unauthorized remote execution, credential theft, or destructive payloads are implemented or will be accepted. Red-team-adjacent features require explicit local arming, use only benign/lab-safe payloads, and are scoped to owned/authorized systems. See [docs/red-team-lab-use-cases.md](docs/red-team-lab-use-cases.md).

---

## License

MIT — see [LICENSE](LICENSE).
