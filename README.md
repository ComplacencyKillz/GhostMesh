# GhostMesh

**A deployable red-team mesh platform — planted sensors, encrypted command-and-control, line-of-sight control, and a destruct — running on nothing the target owns.**

GhostMesh is a **backpack that snaps onto a Flipper Zero — then detaches and stays in the field.** Attached, it turns your Flipper into a long-range mesh radio: messaging and control over LoRa, no phone, no cell, no internet, nothing the target controls. Detached and planted, it becomes an autonomous node — watching its own perimeter, holding its place on the mesh, and taking orders by line-of-sight IR from you or over the encrypted mesh from your team. Drop it, walk away, and it stays yours.

This repository is the **framework** — the sensing, signaling, and command backbone that offensive capability is built on top of. It reports, it takes commands, and it erases itself on capture. Unlicensed by design.

---

## What's Here

Everything to build one, end to end — all of it open source:

| Deliverable | Location | Status |
|-------------|----------|--------|
| **Operator app** — the Flipper FAP (C99) | [`flipper-app/`](flipper-app/) | working |
| **Backpack firmware** — custom Heltec Meshtastic modules (C++) | [`heltec-firmware/`](heltec-firmware/) | working |
| **Board CAD** — the GhostMesh PCB | [`kicad/`](kicad/) | in progress |
| **Case CAD** — 3D-printable enclosure (FreeCAD) | — | planned |
| **Hardware spec** — every component, datasheet-trackable | [`docs/hardware.md`](docs/hardware.md) | in progress |
| **Wiring schematic** — the full system, end to end | [`kicad/`](kicad/) · [`docs/wiring.md`](docs/wiring.md) | in progress |

The two software halves work today. The board is being designed; the case, the finished component spec, and the whole-system schematic follow it. The transmission — [ghostmesh.info](ghostmesh.info/).

---

## How It Works

The backpack is a **shield.** It plugs onto the Flipper Zero's GPIO header, taps the three pins it needs — TX, RX, GND — and passes the header through to the Heltec and sensor stack riding on top. One board. It runs from its own battery; the Flipper runs from its own. The header is the only link, and it's a link you can break on purpose.

<pre><code>
   ATTACHED — comms terminal            DETACHED — planted node
  ┌──────────────────────┐            ┌──────────────────────┐
  │  backpack             │  pull off  │  backpack             │  ))) mesh → teammate's Flipper
  │  Heltec + sensors     │  ───────▶  │  on its own battery   │
  │ ══ GPIO header ══     │  ◀───────  │  sensing · on mesh    │   ~~ IR → your Flipper
  │  Flipper Zero         │  snap on   │                       │
  └──────────────────────┘            └──────────────────────┘
</code></pre>

**Attached — a comms terminal.** The backpack rides on the Flipper, which drives it over the header link: send and receive over the LoRa mesh, read telemetry, work the Control screen. Your handheld is now a long-range, infrastructure-free field radio.

**Detached — a planted node.** Pull the backpack off. The header disconnects; it keeps running on its own battery as an isolated asset — sensing its surroundings, holding its place on the mesh, waiting.

**Controlled from anywhere.**
- The **original operator** reaches the planted node by **line-of-sight IR** — arm, disarm, or fire the destruct without touching it.
- **Teammates** reach it over the **encrypted LoRa mesh** — status, indicators, wipe, from any Flipper on the private channel.

---

## What Works Today

**Operator terminal — Flipper FAP**
- Send / receive text over the mesh; canned message profiles (3 built-in + up to 5 from SD YAML)
- RX history, RSSI / SNR, marquee display, dated CSV logging (→ KML via `tools/log_to_kml.py`)
- Live telemetry: temperature / humidity / pressure (BME280), GPS position, battery %
- Menu-hub UI with **Control** (IR arm / disarm / wipe), **encrypted config backup**, and a live **Settings** screen

**Web configurator — [ghostmesh.info/config](https://ghostmesh.info/config)** (Chrome/Edge, USB, no install)
- Live node config over Web Serial (`/set`/`/cfg`) — every setting as a slider/toggle
- **Firmware flasher** (esptool-js) — flash the latest build or your own `.bin` from the browser
- **Payload Upload** — `/put` chunked file transfer to the node's flash, CRC32-verified

**Backpack — custom Heltec firmware**
- Tamper detection: tilt (moved), photoresistor (case opened), ultrasonic proximity (RCWL-1601, approach)
- Arming gate — sensors report only when armed; flipped by a switch, the mesh, or IR
- Indicators: buzzer, vibration motor, RGB status LED (colors + a green↔red gradient)
- Mesh command CLI — `/cmd @target`: status, arm/disarm, buzz, vibrate, led, fx, `/set`/`/cfg`, `/put`, wipe
- **Configurable everything**: per-command mesh replies on/off, a `silent` stealth mode (screen + LEDs + buzzer/vibration off), per-sensor battery gating, GPS/telemetry rate control — all live, no reflash
- IR line-of-sight control (NECext): arm / disarm / the `ARM → WIPE → CONFIRM` destruct
- Destruct: armed-gated **complete flash erase** — firmware, config, and channel keys wiped to USB download mode; recover by reflash + encrypted-backup restore

**Hardware**
- KiCad schematics for the Flipper and Heltec modules (board layout in progress)

Full phase-by-phase status: [`docs/roadmap.md`](docs/roadmap.md).

---

## What You Need

The **GhostMesh backpack** — the custom board that integrates all of this into one Flipper shield — is under active development ([`kicad/`](kicad/)). Until it's fabricated, you build on off-the-shelf parts:

**Core — send / receive over the mesh:**
- Flipper Zero
- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) + a 915 MHz antenna
- A LiPo for the Heltec (independent of the Flipper battery)
- A PINGEQUA protoboard to seat the Heltec on the Flipper's GPIO header — the dev stand-in for the real board, not the product

**Full backpack — sensing + security:** adds a BME280, a GPS module, tilt / photoresistor / proximity sensors, an IR receiver, a buzzer, a vibration motor, and their driver parts.

The complete bill of materials — every component, GPIO, and driver circuit — is in [`docs/hardware.md`](docs/hardware.md).

---

## Quick Start

### 1. Flash Meshtastic to the Heltec
Visit [flasher.meshtastic.org](https://flasher.meshtastic.org), select **Heltec WiFi LoRa 32 V3**, flash, and set your region. See [`docs/meshtastic-setup.md`](docs/meshtastic-setup.md) for the private-channel configuration you'll want before any real use.

### 2. Connect the backpack (three signals)
On the backpack PCB these run through the GPIO header automatically. On the bench, jumper them:
<pre><code>
Flipper U_TX (pin 13)  →  Heltec GPIO7  (Serial module RX)
Flipper U_RX (pin 14)  →  Heltec GPIO6  (Serial module TX)
Flipper GND            →  Heltec GND
</code></pre>
Then configure the Meshtastic **Serial module**: PROTO mode, RX 7, TX 6, 115200, override-console OFF. **Not GPIO43/44** — the CP2102 USB bridge clamps those on battery. **Never bridge the power rails** — the backpack and Flipper each run from their own battery. See [`docs/wiring.md`](docs/wiring.md).

### 3. Build and install the FAP
<pre><code>
pip install ufbt
cd flipper-app
ufbt
</code></pre>
Copy `dist/ghostmesh.fap` to `SD:/apps/Tools/` on the Flipper, or `ufbt launch` over USB. See [`docs/flipper-setup.md`](docs/flipper-setup.md).

### 4. (Optional) Build the backpack firmware
The custom Heltec modules build on top of stock Meshtastic. See [`heltec-firmware/README.md`](heltec-firmware/README.md).

### 5. Run
**Apps → Tools → GhostMesh.** The title bar shows `...` during the handshake (~3 s), then `RDY`, then the backpack's battery %. You land on the menu hub — Messages, RX History, Sensors, Control, Status, Backup. See [`docs/user-guide.md`](docs/user-guide.md).

---

## Documentation

**Start here**
- [`docs/overview.md`](docs/overview.md) — the system end to end: architecture, concepts, the platform vision

**Build & operate**
- [`docs/hardware.md`](docs/hardware.md) — full BoM, GPIO allocation, power
- [`docs/wiring.md`](docs/wiring.md) — pinouts, driver circuits, sensor wiring
- [`docs/meshtastic-setup.md`](docs/meshtastic-setup.md) — Meshtastic config, private channels
- [`docs/flipper-setup.md`](docs/flipper-setup.md) — ufbt build & install
- [`heltec-firmware/README.md`](heltec-firmware/README.md) — building the backpack firmware
- [`docs/user-guide.md`](docs/user-guide.md) — the FAP, screen by screen

**Operate the backpack**
- [`docs/command-cli.md`](docs/command-cli.md) — the mesh command CLI + wipe safety
- [`docs/opsec.md`](docs/opsec.md) — encryption, the destruct, stealth, metadata

**Reference**
- [`docs/developer-guide.md`](docs/developer-guide.md) — architecture, ISR rules, contributing
- [`docs/serial-modes.md`](docs/serial-modes.md) — PROTO framing & protobuf field numbers
- [`docs/roadmap.md`](docs/roadmap.md) — the phased plan (Phases 0–14)
- [`docs/red-team-lab-use-cases.md`](docs/red-team-lab-use-cases.md) — authorized use cases

---

## License

MIT — see [LICENSE](LICENSE).

<pre><code>
// so light your candles
</code></pre>
