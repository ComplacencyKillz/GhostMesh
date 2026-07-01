# Meshtastic Setup

## Prerequisites

- Heltec WiFi LoRa 32 V3 (or MakerHawk ESP32 LoRa V3 compatible) flashed with Meshtastic firmware
- Meshtastic mobile app (iOS or Android) connected to the node via Bluetooth
- Antenna attached before powering the radio
- Node confirmed working — visible in the app and able to send/receive messages

---

## Flash Meshtastic (if not already done)

1. Download the Meshtastic flasher: https://flasher.meshtastic.org
2. Connect the Heltec board via USB-C
3. Select **Heltec WiFi LoRa 32 V3** as the target device
4. Flash the latest stable firmware
5. Open the Meshtastic app, connect via Bluetooth, complete initial setup (region, name)

---

## Serial Module — Required Configuration

GhostMesh connects over the Meshtastic **Serial module in PROTO mode** on free GPIO pins. This is **required** — set it under **Module Config → Serial**. (Do this over the Meshtastic **web client over USB** — `client.meshtastic.org` → Serial — it's far more reliable than Bluetooth, which times out on config screens.)

| Field | Value |
|-------|-------|
| Serial enabled | **ON** |
| Serial mode | **PROTO** |
| RX | **7** |
| TX | **6** |
| Serial baud rate | **115200** |
| Override console serial port | **OFF** |
| Echo enabled | OFF |

Save → the node reboots with the PROTO stream on GPIO7 (RX) / GPIO6 (TX), matching the wires in [wiring.md](wiring.md).

> **Why not UART0 / GPIO43-44 (the old PhoneAPI path)?** Earlier builds connected to the PhoneAPI on UART0, but the **CP2102 USB bridge shares those pins** and clamps them when the Heltec is on battery (USB unplugged) — so that link only worked while USB-powered, which is no good for a deployed backpack. The Serial module on free pins 6/7 has no CP2102 in the way and works on pure battery. The old "SerialModule PROTO is unreliable" advice was a misdiagnosis of that same CP2102 clamp on 43/44.

---

## Required Settings

### Region

Set your region under **Settings → Radio Config → LoRa → Region**. The 915 MHz antenna is tuned for the US ISM band (902–928 MHz).

| Region | Frequency band |
|--------|---------------|
| US | 902–928 MHz |
| EU_868 | 863–870 MHz |
| AU_915 | 915–928 MHz |

Using the wrong region with a mismatched antenna risks poor RF performance.

### Channel

The default **LongFast** channel uses a publicly known key (`AQ==`) — any Meshtastic
node can read your traffic. For operational use, create a private channel with a random
key. See [docs/opsec.md](opsec.md) for the full setup procedure.

For basic testing, the default channel is fine.

---

## Verifying the Node is Ready

- Node appears in Meshtastic app with name and battery level
- Node can send a test message from the phone app (cloud icon confirms transmission)
- Heltec OLED shows node name, battery %, and ChUtil

When GhostMesh connects:
1. The Flipper title bar shows `...` for a few seconds (config handshake in progress; the request self-retries every ~2 s until the node answers)
2. It changes to `RDY` when the ~47-frame config exchange completes, then to the node's battery `%` (or `PWR` when on external power) once the battery level is read from the config
3. The OK button becomes active

---

## Uploading Custom Profiles

Place a `profiles.yaml` file at:
```
SD:/apps_data/ghostmesh/profiles.yaml
```

Example format:
```yaml
# GhostMesh custom profiles

name: My Red Team Profile
- CHECKIN OK
- IN POSITION
- ABORT
- EXFIL NOW

name: Grid Down Custom
- CHECKIN OK
- NEED ASSISTANCE
- MOVING
```

See `examples/profiles.yaml` in the GhostMesh repo for a fully commented template.

Up to 5 custom profiles are loaded alongside the 3 built-ins (8 total). Profiles with no messages are silently discarded.

---

## Sensor Module Configuration (Phases 7+)

When sensor hardware is added to the Heltec, enable the corresponding Meshtastic modules
in the app. No custom firmware needed for these:

| Sensor | Meshtastic setting | Path in app |
|--------|--------------------|-------------|
| BME280 (temp/humidity/pressure) | Enable Environment Telemetry | Module Config → Telemetry → Environment |
| BN-220 GPS | Enable GPS, set GPS Receive GPIO=34, Transmit GPIO=33 | Module Config → Position → Advanced |

See [docs/hardware.md](hardware.md) for full sensor wiring and GPIO assignments.

---

## Known Hardware Notes (Heltec V3 + Meshtastic 2.7.x)

**GPIO pin conflicts discovered during development:**

| GPIO | Status | Reason |
|------|--------|--------|
| 7 | ✓ Used by GhostMesh (Serial module RX) | connect Flipper TX (pin 13) here |
| 6 | ✓ Used by GhostMesh (Serial module TX) | connect Flipper RX (pin 14) here |
| 43/44 | Avoid for the Flipper link | UART0 / CP2102 USB console — clamps on battery (USB debug only) |
| 41/42 | Unavailable | Claimed by I2C bus 2 (`sda=41 scl=42`) at Meshtastic boot |
| 19/20 | Unavailable | ESP32-S3 USB D-/D+ |
| 8–14 | Unavailable | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 1 | Unavailable | Battery ADC |

**GhostMesh runs the Serial module in PROTO mode on GPIO7/6.** An earlier version of this doc claimed the SerialModule PROTO mode "does not work reliably" and used UART0/GPIO43/44 instead — that was a **misdiagnosis**. The SerialModule was originally configured on GPIO43/44, where the CP2102 USB bridge clamps the lines whenever the Heltec is on battery. On free pins (6/7) with no CP2102, PROTO mode is reliable and works on pure battery. Confirmed 2026-07-01.
