---
---
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

GhostMesh connects over the Meshtastic **Serial module in PROTO mode** on free GPIO pins. This is **required** — set it under **Module Config → Serial**. (Do this over the Meshtastic **web client over USB** — <code>client.meshtastic.org</code> → Serial — it's far more reliable than Bluetooth, which times out on config screens.)

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

The default **LongFast** channel uses a publicly known key (<code>AQ==</code>) — any Meshtastic
node can read your traffic. For operational use, create a private channel (custom name +
random key). See [docs/opsec.md](opsec.md) for the full setup procedure.

> **Two gotchas when moving off the default channel:**
> 1. **The Detection Sensor module will not broadcast on the default/public channel** —
>    Meshtastic blocks it by design (<code>isDefaultChannel</code> gate). Tamper alerts only send on a
>    non-default primary channel. Renaming off "LongFast" is enough to unblock it; a random
>    key adds actual privacy.
> 2. **After renaming a channel, both nodes must share a Frequency Slot.** Slot <code>0</code> =
>    auto-derived from the channel *name*, so a rename can move one node to a new slot (e.g.
>    slot 20 → 64, i.e. 906.875 → 917.875 MHz). Two nodes on different slots can't hear each
>    other — messages show "undelivered" even with identical name + key. Fix: set **Frequency
>    Slot = 0 on both** (same name → same slot), or the same explicit slot number on both
>    (LoRa → Advanced → Frequency Slot).

For basic testing, the default channel is fine — but note the Detection Sensor won't send on it.

---

## Verifying the Node is Ready

- Node appears in Meshtastic app with name and battery level
- Node can send a test message from the phone app (cloud icon confirms transmission)
- Heltec OLED shows node name, battery %, and ChUtil

When GhostMesh connects:
1. The Flipper title bar shows <code>...</code> for a few seconds (config handshake in progress; the request self-retries every ~2 s until the node answers)
2. It changes to <code>RDY</code> when the ~47-frame config exchange completes, then to the node's battery <code>%</code> (or <code>PWR</code> when on external power) once the battery level is read from the config
3. The OK button becomes active

---

## Uploading Custom Profiles

Place a <code>profiles.yaml</code> file at:
<pre><code>
SD:/apps_data/ghostmesh/profiles.yaml
</code></pre>

Example format:
<pre><code>
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
~~~

See <code>examples/profiles.yaml</code> in the GhostMesh repo for a fully commented template.

Up to 5 custom profiles are loaded alongside the 3 built-ins (8 total). Profiles with no messages are silently discarded.

---

## Sensor Module Configuration (Phases 7+)

When sensor hardware is added to the Heltec, enable the corresponding Meshtastic modules
in the app. No custom firmware needed for these:

| Sensor | Meshtastic setting | Path in app |
|--------|--------------------|-------------|
| BME280 (temp/humidity/pressure) | Enable Environment Telemetry | Module Config → Telemetry → Environment |
| BN-220 GPS | Enable GPS, set GPS Receive GPIO=34, Transmit GPIO=33 | Module Config → Position → Advanced |
| SW-520D tilt switch (tamper) | Enable Detection Sensor (see below) | Module Config → Detection Sensor |

See [docs/hardware.md](hardware.md) for full sensor wiring and GPIO assignments.

### Detection Sensor — Digital Tamper Switch (tilt / slide)

> **Note:** GhostMesh's deployed Heltec firmware now uses the custom <code>TiltModule</code>
> (<code>heltec-firmware/</code>) for the tilt, so the built-in Detection Sensor should be **disabled**
> (it can't be arm-gated). This section is kept as reference — the built-in module still works
> for a standalone digital switch, and the **channel requirements above apply to the custom
> modules too.**

A single digital switch (tilt, reed, slide) can broadcast a tamper alert over LoRa using the
**built-in Detection Sensor module** — no custom firmware. Module Config → Detection Sensor:

| Field | Value | Notes |
|-------|-------|-------|
| Detection Sensor enabled | ON | |
| Monitor Pin | 2 | GPIO the switch is wired to (tilt = GPIO2) |
| Use INPUT_PULLUP | OFF | the board has an external 10kΩ pull-down (see <code>kicad/</code>) |
| Detection trigger type | EITHER_EDGE_ACTIVE_HIGH | switch-closed pulls the pin HIGH with the external pull-down |
| Minimum broadcast (seconds) | 30 | anti-spam rate limit (see below) |
| Friendly name | TAMPER | used in the alert text (<code>TAMPER detected</code>) |

On a state change it broadcasts a text mesh packet — <code>TAMPER detected</code> on the active edge, and
<code>TAMPER state: 0</code> on the return edge (with EITHER_EDGE). The GhostMesh FAP receives these as
ordinary mesh text; treat any message from the sensor as "disturbed."

**Requires a private channel** — see the two Channel gotchas above; it will not send on the
default public channel, and both nodes must be on the same frequency slot.

**Re-trigger behavior:** after each alert the module is silent for the Minimum Broadcast
interval (30 s) and does not poll the pin during that window, so reliable re-triggering means
waiting the full interval, then a deliberate tilt-and-hold. Lower the interval for bench testing.

> Use an **edge** trigger, not a level trigger (LOGIC_HIGH/LOW): a ball tilt/vibration switch
> (SW-520D) chatters, and level triggers can miss the momentary contacts.

---

## Known Hardware Notes (Heltec V3 + Meshtastic 2.7.x)

**GPIO pin conflicts discovered during development:**

| GPIO | Status | Reason |
|------|--------|--------|
| 7 | ✓ Used by GhostMesh (Serial module RX) | connect Flipper TX (pin 13) here |
| 6 | ✓ Used by GhostMesh (Serial module TX) | connect Flipper RX (pin 14) here |
| 43/44 | Avoid for the Flipper link | UART0 / CP2102 USB console — clamps on battery (USB debug only) |
| 41/42 | Unavailable | Claimed by I2C bus 2 (<code>sda=41 scl=42</code>) at Meshtastic boot |
| 19/20 | Unavailable | ESP32-S3 USB D-/D+ |
| 8–14 | Unavailable | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 1 | Unavailable | Battery ADC |

**GhostMesh runs the Serial module in PROTO mode on GPIO7/6.** An earlier version of this doc claimed the SerialModule PROTO mode "does not work reliably" and used UART0/GPIO43/44 instead — that was a **misdiagnosis**. The SerialModule was originally configured on GPIO43/44, where the CP2102 USB bridge clamps the lines whenever the Heltec is on battery. On free pins (6/7) with no CP2102, PROTO mode is reliable and works on pure battery. Confirmed 2026-07-01.
