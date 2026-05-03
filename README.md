# GhostMesh

**GhostMesh: a Flipper Zero companion interface for offline Meshtastic field communications.**

GhostMesh turns your Flipper Zero into a handheld controller for a Meshtastic-enabled ESP32 LoRa node, enabling encrypted mesh radio messaging without any phone, internet, or cell infrastructure.

---

## What It Does

- Connects the Flipper Zero to a Meshtastic node over UART (pins 13/14)
- Displays live UART status and RX/TX byte counters (v0.1)
- Sends canned field messages over the Meshtastic mesh network (v0.2)
- Field profiles for grid-down, hiking, and red-team lab scenarios (v0.3)
- Designed for authorized offline comms, field diagnostics, and lab-controlled red-team support

---

## Hardware

| Component | Details |
|-----------|---------|
| Flipper Zero | With official prototype board |
| ESP32 LoRa node | MakerHawk / Heltec WiFi LoRa 32 V3 compatible |
| Radio | SX1262, 915 MHz antenna |
| Heltec battery | Connected directly to Heltec — not from Flipper |
| Flipper battery | Flipper runs from its own battery |

The Heltec board must be pre-flashed with [Meshtastic firmware](https://meshtastic.org/).

See [docs/hardware.md](docs/hardware.md) and [docs/wiring.md](docs/wiring.md) for full details.

---

## Quick Start

### 1. Wire the hardware

```
Flipper U_TX (pin 13)  →  Heltec RX
Flipper U_RX (pin 14)  →  Heltec TX
Flipper GND            →  Heltec GND
NO shared power        —  each device runs from its own battery
```

See [docs/wiring.md](docs/wiring.md) for the full pinout table and safety notes.

### 2. Configure Meshtastic serial mode

In the Meshtastic mobile app: **Settings → Module Config → Serial → Mode → TEXTMSG**

Baud rate: **115200**, enabled: **true**

See [docs/meshtastic-setup.md](docs/meshtastic-setup.md) for step-by-step instructions.

### 3. Install ufbt and build the FAP

```bash
python -m pip install --upgrade ufbt
cd flipper-app
ufbt
```

The compiled `.fap` will appear in `flipper-app/dist/`. Copy it to `SD:/apps/Tools/GhostMesh.fap` on your Flipper.

### 4. Run the app

On the Flipper: **Apps → Tools → GhostMesh**

The app opens UART at 115200 baud and shows RX/TX byte counters. Press **OK** to send a test message over the mesh.

---

## Serial Mode

GhostMesh v0.1 targets **TEXTMSG** mode: plain UTF-8 lines over UART, no framing required.  
Full PROTO mode (protobuf serial framing) is the long-term upgrade path.

See [docs/serial-modes.md](docs/serial-modes.md) for a full comparison and migration plan.

---

## Project Structure

```
ghostmesh/
├── README.md
├── LICENSE
├── docs/
│   ├── hardware.md           Hardware specs and compatibility
│   ├── wiring.md             Exact pinout for this hardware setup
│   ├── meshtastic-setup.md   Meshtastic serial mode configuration
│   ├── flipper-setup.md      ufbt build and install instructions
│   ├── serial-modes.md       TEXTMSG vs SIMPLE vs PROTO analysis
│   ├── roadmap.md            Phased development plan
│   └── red-team-lab-use-cases.md  Authorized lab use cases (docs only)
├── flipper-app/
│   ├── application.fam       FAP metadata
│   ├── ghostmesh.c           App entry point and main loop
│   ├── helpers/
│   │   ├── uart_helper.h/.c  UART init, TX, RX abstraction
│   │   ├── textmsg_mode.h/.c TEXTMSG message formatting
│   │   └── proto_notes.md    Notes on future PROTO support
│   └── views/
│       └── main_view.h/.c    Primary status/counter UI view
├── examples/
│   ├── canned-messages.json  Default canned message list
│   └── field-profiles.json   Field deployment profiles
├── tools/
│   └── log_to_kml.py         Convert node/RSSI logs to KML
└── tests/
    └── uart-test-plan.md     Manual UART validation procedures
```

---

## Roadmap

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Documentation and hardware sanity checks | Done |
| 1 | UART byte-counter FAP (compilable v0.1) | Done |
| 2 | Canned message menu + TEXTMSG send/receive | Planned |
| 3 | Field profiles from SD card | Planned |
| 4 | Node log + KML export | Planned |
| 5 | PROTO mode client (nanopb) | Future |

See [docs/roadmap.md](docs/roadmap.md) for full milestone details.

---

## Safety and Scope

This project is for **authorized security work, personal lab testing, grid-down comms experimentation, and open-source learning only.**

No malware, unauthorized remote execution, credential theft, or destructive payloads are implemented or will be accepted. Any red-team-adjacent features require explicit local arming, use only benign/lab-safe payloads, and are scoped to owned/authorized systems.

See [docs/red-team-lab-use-cases.md](docs/red-team-lab-use-cases.md) for the full scope statement.

---

## License

MIT — see [LICENSE](LICENSE).
