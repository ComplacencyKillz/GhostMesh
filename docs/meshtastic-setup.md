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

## Serial Module — No Configuration Required

GhostMesh connects directly to Meshtastic's **PhoneAPI on UART0** (the same path used by the official Meshtastic Python library and the phone app over USB). This is always active and requires no special configuration.

The **Module Config → Serial** settings in the Meshtastic app configure a separate GPIO-based serial module that GhostMesh does not use. You can leave these at defaults or disable the serial module — it has no effect on GhostMesh.

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
1. The Flipper title bar shows `...` for a few seconds (config handshake in progress)
2. It changes to `RDY` when the ~47-frame config exchange completes
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
| 44 | ✓ Used by GhostMesh (UART0 RX) | PhoneAPI RX — connect Flipper TX here |
| 43 | ✓ Used by GhostMesh (UART0 TX) | PhoneAPI TX — connect Flipper RX here |
| 41/42 | Unavailable | Claimed by I2C bus 2 (`sda=41 scl=42`) at Meshtastic boot |
| 19/20 | Unavailable | ESP32-S3 USB D-/D+ |
| 8–14 | Unavailable | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 1 | Unavailable | Battery ADC |

**The Meshtastic SerialModule PROTO mode via GPIO does not work reliably in Meshtastic 2.7.x.** GhostMesh bypasses the SerialModule entirely by connecting to UART0 (GPIO43/44), which is where the PhoneAPI lives permanently.
