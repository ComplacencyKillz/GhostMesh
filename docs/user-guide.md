# GhostMesh User Guide

## Overview

GhostMesh has four screens. You start at the Profile screen every time the app launches.

```
Profile screen ─[OK]→ Message screen ─[long Down]→ RX History screen
(BACK exits app)          │      ↑                   (BACK returns)
                          └─[long Up]→ Sensors screen (BACK returns)
```

---

## Profile Screen

The first screen you see. Lists all loaded profiles (3 built-ins + up to 5 custom).

```
┌────────────────────────────┐
│ GhostMesh             RDY  │
├────────────────────────────┤
│ ► Grid Down                │
│   Hiking / SAR             │
│   Red Team                 │
│   My Custom Profile        │
├────────────────────────────┤
│ OK: Load   BACK: Exit      │
└────────────────────────────┘
```

| Button | Action |
|--------|--------|
| UP / DOWN | Scroll through profiles (hold for repeat) |
| OK | Load the highlighted profile → go to Message screen |
| BACK | Exit GhostMesh |

**Status indicator (top right):**
- `...` — handshake in progress (self-retries every ~2 s until the node answers)
- `RDY` — connected to the Meshtastic node, ready to send
- `77%` / `PWR` — the node's battery level once known (`PWR` = on external/USB power)

Profile names longer than the display width marquee-scroll automatically.

---

## Message Screen

The main operating screen. Shows the canned messages for the loaded profile.

```
┌────────────────────────────┐
│ Grid Down             RDY  │
├────────────────────────────┤
│ ► CHECKIN OK               │
│   NEED ASSISTANCE          │
│   MOVING                   │
│   HOLD POSITION            │
├────────────────────────────┤
│ f69c: CHECKIN OK           │ ← last received message
└────────────────────────────┘
```

| Button | Action |
|--------|--------|
| UP / DOWN | Scroll through messages (hold for repeat) |
| OK | Send the highlighted message over the mesh |
| BACK | Return to Profile screen |
| Long-press DOWN | Open RX History screen (only when messages have been received) |
| Long-press UP | Open Sensors screen (BME280 telemetry + GPS) |

**Status bar (bottom):**
- `TX:N  [OK] Send` — no messages received yet; shows TX byte count
- `sender: message` — last received message (e.g., `f69c: CHECKIN OK`)
- `Sent: message` — 2-second confirmation banner after you send

Long text in both the message list and the status bar marquee-scrolls to reveal the full content.

**Scrollbar:** appears on the right edge when the profile has more messages than fit on screen.

---

## RX History Screen

Opened with long-press Down from the Message screen. Shows the last 16 received messages, newest at the top.

```
┌────────────────────────────┐
│ RX History            RDY  │
├────────────────────────────┤
│ f69c -3dBm: CHECKIN OK     │
│ 2f74: MOVING               │
│ f69c -7dBm: ALL CLEAR      │
│ 2f74: NEED WATER           │
├────────────────────────────┤
│ BACK: Return               │
└────────────────────────────┘
```

Each entry shows the sender's node ID (last 4 hex digits), RSSI in dBm (when available), and the message text. All entries marquee-scroll if the text is too long.

| Button | Action |
|--------|--------|
| UP / DOWN | Scroll through history |
| BACK | Return to Message screen |

---

## Sensors Screen

Opened with long-press Up from the Message screen. Shows the latest environmental telemetry and GPS position reported by the local Heltec node.

```
┌────────────────────────────┐
│ Sensors               77%  │
├────────────────────────────┤
│ Temp:  23.4 C              │
│ Humid: 41%                 │
│ Press: 1013.2 hPa          │
│ GPS 37.0432,-76.3263       │
│ Alt: 27 m                  │
└────────────────────────────┘
```

- Temp / Humid / Press come from the BME280 (Environment Telemetry). Shown once an `environment_metrics` packet has been received; blank until then.
- The GPS line shows the last-known fix, or `GPS: no fix` before the node has a lock.

| Button | Action |
|--------|--------|
| BACK | Return to Message screen |

---

## SD Card Logging

Every received message is automatically appended to a dated CSV file on the Flipper SD card:

```
SD:/apps_data/ghostmesh/log_YYYYMMDD.csv
```

**CSV columns:** `timestamp, node_id, message, lat, lon, rssi, snr`

Example:
```
timestamp,node_id,message,lat,lon,rssi,snr
2026-05-06T14:32:01,f69c,CHECKIN OK,37.0432650,-76.3262981,-85,7.5
2026-05-06T14:33:44,2f74,MOVING,,,-92,4.2
```

`lat` / `lon` hold the local node's last-known GPS fix (degrees), or are left blank when there is no fix yet.

A new file is created each day. The file header is written automatically on first creation.

### Converting to KML

```bash
python tools/log_to_kml.py log_20260506.csv
```

This generates `log_20260506.kml` which you can open in Google Earth or QGIS. Rows with `lat`/`lon` populated are plotted at their reported position; rows logged before a GPS fix have blank coordinates and are placed at 0,0 with a note.

---

## Custom Profiles

Create `SD:/apps_data/ghostmesh/profiles.yaml` on your Flipper SD card. Use the `name:` keyword to start a profile, then list messages with `-`:

```yaml
name: My Field Profile
- CHECKIN OK
- IN POSITION
- MOVING
- ABORT
- EXFIL NOW
- NEED MEDICAL

name: Comms Check
- RADIO CHECK
- LOUD AND CLEAR
- WEAK SIGNAL
```

**Rules:**
- Up to 5 custom profiles, up to 12 messages per profile
- Profile names: max 19 characters, printable ASCII only
- Messages: max 22 characters, printable ASCII only
- Profiles with no valid messages are silently skipped
- Custom profiles load alongside the 3 built-ins (8 total max)

See `examples/profiles.yaml` for a fully commented template.

---

## Built-In Profiles

### Grid Down
`CHECKIN OK` · `NEED ASSISTANCE` · `MOVING` · `HOLD POSITION` · `ALL CLEAR` · `BATTERY LOW` · `MEDICAL NEEDED` · `SHELTER IN PLACE`

### Hiking / SAR
`CHECKIN OK` · `ON TRAIL` · `OFF TRAIL` · `SUMMIT REACHED` · `TURNING BACK` · `NEED WATER` · `NEED MEDICAL` · `CAMP REACHED`

### Red Team
`CHECKIN OK` · `IN POSITION` · `MOVING` · `ABORT` · `PHASE START` · `PHASE COMPLETE` · `HOLD` · `ALL CLEAR`

---

## Understanding RSSI

RSSI (Received Signal Strength Indicator) appears in the RX History screen next to the sender ID. It is measured in dBm (decibels relative to one milliwatt).

| RSSI range | Signal quality |
|------------|----------------|
| -30 to -70 dBm | Strong (close proximity) |
| -70 to -100 dBm | Good (normal LoRa range) |
| -100 to -120 dBm | Marginal |
| Below -120 dBm | Very weak / edge of range |

RSSI of `0` means the packet did not arrive over radio (local echo or phone app origin).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Title bar shows `...` indefinitely | No UART connection, or Serial module not set to PROTO on 7/6 | Check TX/RX wires (Flipper 13→Heltec 7, 14→Heltec 6); verify the Meshtastic Serial module is enabled in PROTO mode |
| Title bar reaches `...` only when the Heltec is plugged into USB | Wired to GPIO43/44 (CP2102 clamps them on battery) | Move the Heltec-side wires to GPIO7/6 |
| OK button sends but nothing heard on mesh | Heltec not transmitting | Check antenna connected; verify Meshtastic region |
| Received messages not appearing | RX wire not connected | Check Flipper pin 14 → Heltec GPIO6 |
| Custom profiles not loading | Wrong SD path or YAML syntax | File must be at `SD:/apps_data/ghostmesh/profiles.yaml` |
| App crashes on launch | Stack issue | Close all other Flipper apps; restart Flipper |
| Log file not created | SD card issue | Verify SD card seated; check available space |
