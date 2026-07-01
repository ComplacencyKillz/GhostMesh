# GhostMesh

**GhostMesh: a Flipper Zero companion interface for offline Meshtastic field communications.**

GhostMesh turns your Flipper Zero into a long-range, mesh-radio command terminal by pairing it with a Heltec ESP32 LoRa node running Meshtastic. No phone, no internet, no cell infrastructure required.

---

## What It Does

- Sends canned field messages over LoRa mesh — select from scrollable profiles, press OK
- Receives incoming mesh text messages with sender ID and RSSI displayed
- RX history screen (long-press Down) — last 16 messages with full signal data, scrollable
- Logs every received message to a dated CSV on the Flipper SD card with timestamp, node ID, RSSI, and SNR
- Three built-in profiles: Grid Down, Hiking / SAR, Red Team
- Custom profiles loaded from `profiles.yaml` on the SD card
- All text marquee-scrolls if it doesn't fit the display — nothing is cut off
- `tools/log_to_kml.py` converts the SD card CSV to KML for Google Earth / QGIS

---

## Hardware Required

| Component | Details |
|-----------|---------|
| Flipper Zero | With PINGEQUA or official prototype board |
| Heltec WiFi LoRa 32 V3 | ESP32-S3 + SX1262, Meshtastic firmware |
| 915 MHz antenna | Attached to Heltec before powering on |
| LiPo battery | For Heltec — independent of Flipper battery |

See [docs/hardware.md](docs/hardware.md) for the full hardware reference and planned sensor expansion (GPS, BME280, tamper sensors, etc.).

---

## Quick Start

### 1. Flash Meshtastic to the Heltec

Visit [flasher.meshtastic.org](https://flasher.meshtastic.org), select **Heltec WiFi LoRa 32 V3**, and flash. Set your region in the Meshtastic app before proceeding.

See [docs/meshtastic-setup.md](docs/meshtastic-setup.md) for full setup and private channel configuration.

### 2. Wire the devices

```
Flipper U_TX (pin 13)  →  Heltec GPIO7  (Serial module RX)
Flipper U_RX (pin 14)  →  Heltec GPIO6  (Serial module TX)
Flipper GND            →  Heltec GND
```

Then configure the Meshtastic **Serial module**: PROTO mode, RX 7, TX 6, 115200, override-console OFF. (Not GPIO43/44 — the CP2102 USB bridge clamps those on battery.) **Do not connect power rails** — each device runs from its own battery. See [docs/wiring.md](docs/wiring.md) and [docs/meshtastic-setup.md](docs/meshtastic-setup.md).

### 3. Build and install

```bash
pip install ufbt
cd flipper-app
ufbt
```

Copy `dist/ghostmesh.fap` to `SD:/apps/Tools/` on your Flipper, or run `ufbt launch` with the Flipper connected via USB.

See [docs/flipper-setup.md](docs/flipper-setup.md) for the full build guide.

### 4. Run

**Apps → Tools → GhostMesh**

The title bar shows `...` during the startup handshake (~3 seconds), then `RDY`. Select a profile, navigate the message list, press OK to send.

See [docs/user-guide.md](docs/user-guide.md) for a screen-by-screen walkthrough.

---

## Custom Profiles via SD Card

Create `SD:/apps_data/ghostmesh/profiles.yaml`:

```yaml
name: My Profile
- CHECKIN OK
- IN POSITION
- ABORT
- EXFIL NOW

name: Another Profile
- MESSAGE ONE
- MESSAGE TWO
```

Up to 5 custom profiles load alongside the 3 built-ins (8 total). See `examples/profiles.yaml` for the full documented template.

---

## Project Structure

```
ghostmesh/
├── README.md
├── docs/
│   ├── user-guide.md           Screen-by-screen user manual
│   ├── developer-guide.md      Code architecture and contribution guide
│   ├── hardware.md             Full hardware reference and BOM
│   ├── wiring.md               Pinout, connections, sensor wiring
│   ├── meshtastic-setup.md     Meshtastic config, private channels
│   ├── flipper-setup.md        ufbt build and install
│   ├── opsec.md                Encryption, stealth, nuke protocol
│   ├── serial-modes.md         PROTO protocol field numbers
│   ├── roadmap.md              Phased development plan (Phases 0–14)
│   └── red-team-lab-use-cases.md  Authorized lab use cases
├── flipper-app/
│   ├── application.fam
│   ├── ghostmesh.c             App entry point and main loop
│   ├── helpers/
│   │   ├── proto_mode.h/.c     PROTO encoder/decoder, handshake
│   │   ├── profile_manager.h/.c  Built-in profiles + YAML loader
│   │   ├── log_manager.h/.c    SD card CSV logger
│   │   ├── uart_helper.h/.c    USART1 init and async RX/TX
│   │   └── proto_notes.md      Protocol implementation reference
│   └── views/
│       └── main_view.h/.c      Three-screen UI
├── examples/
│   └── profiles.yaml           Documented custom profile template
├── tests/
│   ├── uart-test-plan.md       Manual hardware validation checklist
│   └── proto_send_test.py      Python PROTO test script
└── tools/
    └── log_to_kml.py           Convert SD card CSV log to KML
```

---

## Protocol

GhostMesh connects over the Meshtastic **Serial module in PROTO mode** (GPIO7 RX / GPIO6 TX), which exposes the same StreamAPI protobuf stream the phone app and Python library use. Packets use Meshtastic's binary PROTO framing (`0x94 0xC3` magic bytes + protobuf payload). All protobuf field numbers are confirmed against meshtastic Python library v2.7.8. (Earlier builds used the PhoneAPI on UART0/GPIO43-44, but the CP2102 USB bridge clamps those pins on battery — 6/7 works untethered.)

See [docs/serial-modes.md](docs/serial-modes.md) for the complete protocol reference.

---

## Operational Security

GhostMesh on the default Meshtastic channel uses a **public encryption key** — traffic is visible to any Meshtastic node. For operational use, create a private channel with a random key before deployment.

See [docs/opsec.md](docs/opsec.md) for the full encryption picture, private channel setup, and the planned nuke / stealth features.

---

## Roadmap

Current version: **v0.5** — PROTO mode full client, SD logging, RX history, marquee scroll.

Planned phases include GPS + wardriving, environmental telemetry, tamper detection, dead-drop surveillance, remote payload execution, and UART encryption. See [docs/roadmap.md](docs/roadmap.md).

---

## Safety and Scope

This project is for **authorized security work, personal lab testing, grid-down comms experimentation, and open-source learning only.**

No malware, unauthorized remote execution, credential theft, or destructive payloads are implemented or will be accepted. See [docs/red-team-lab-use-cases.md](docs/red-team-lab-use-cases.md).

---

## License

MIT — see [LICENSE](LICENSE).
