# GhostMesh Test Plan

## Purpose

Validate the hardware path and PROTO protocol connection between the Flipper Zero and the Heltec ESP32 LoRa V3 before deploying or troubleshooting the GhostMesh FAP.

---

## Confirmed Working Configuration

Verified 2026-05-05.

| Parameter | Value |
|-----------|-------|
| Flipper TX wire | Pin 13 (U_TX) → Heltec **GPIO44** (bottom row, "RX" labeled pad) |
| Flipper RX wire | Pin 14 (U_RX) → Heltec **GPIO43** (bottom row, "TX" labeled pad) |
| GND | Flipper GND → Heltec GND |
| Protocol | Meshtastic PROTO via PhoneAPI on UART0 |
| Baud | 115200 |
| Meshtastic serial module | Not used — leave at defaults or disabled |

> GhostMesh uses the PhoneAPI on UART0, not the Meshtastic serial module. No serial module configuration is required or relevant.

---

## Pre-Test Checklist

- [ ] Flipper U_TX (pin 13) → Heltec **GPIO44** ("RX" labeled pad, UART0 RX)
- [ ] Flipper U_RX (pin 14) → Heltec **GPIO43** ("TX" labeled pad, UART0 TX)
- [ ] GND connected between both boards
- [ ] No 5V or 3.3V shared between boards
- [ ] Heltec powered from its own battery
- [ ] Flipper powered from its own battery
- [ ] Meshtastic running on Heltec (visible in phone app with battery %)
- [ ] Second Meshtastic node powered and in range (for full end-to-end tests)

---

## Test 1: Flipper TX Loopback

**Goal:** Confirm Flipper USART1 transmits on pin 13 before involving the Heltec.

**Steps:**
1. Disconnect TX/RX wires from Heltec
2. Jumper Flipper pin 13 directly to Flipper pin 14
3. Open Flipper: **Apps → GPIO → USB-UART Bridge** at 115200
4. Open PuTTY — **Terminal → Local echo → Force off**
5. Type any character

**Pass criteria:** Character appears on PuTTY screen (looped from pin 13 back in on pin 14). Flipper TX hardware confirmed working.

---

## Test 2: Heltec TX → Flipper RX (boot log)

**Goal:** Confirm the GPIO43 → Flipper RX → PuTTY path is alive.

**Steps:**
1. Reconnect wires (GPIO44 and GPIO43)
2. Enable PuTTY session logging: **Session → Logging → All session output → `log.txt`**
3. Connect PuTTY to the Flipper COM port at 115200
4. Unplug and replug the Heltec battery while PuTTY is connected
5. Open `log.txt`

**Pass criteria:** ESP32 boot ROM messages and Meshtastic startup log appear in the file. This confirms the GPIO43 → Flipper RX direction is working.

---

## Test 3: End-to-End PROTO Send (Python)

**Goal:** Confirm the full handshake and text send chain without the FAP.

**Requirements:** `pip install pyserial` on Windows. Flipper USB-UART Bridge running on Flipper.

**Steps:**
1. From Windows, run:
   ```
   python S:\path\to\ghostmesh\tests\proto_send_test.py COM3
   ```
2. Watch the Heltec OLED for ChUtil change
3. Check the second node's Meshtastic app

**Pass criteria:**
- Script prints `config_complete_id received — ready!` (PROTO handshake complete)
- ChUtil ticks up (LoRa transmitted)
- "TEST FROM PYTHON" appears on second node

---

## Test 4: GhostMesh FAP — Startup and Handshake

**Goal:** Confirm the FAP connects via PROTO on startup.

**Steps:**
1. Close USB-UART Bridge if running (releases USART1)
2. Launch GhostMesh: **Apps → Tools → GhostMesh**
3. Watch top-right of screen for a few seconds

**Pass criteria:** Screen shows `PROTO:...` briefly then changes to `PROTO:RDY`. Profile selection screen appears.

---

## Test 5: GhostMesh FAP — Send via OK Button

**Goal:** Confirm pressing OK broadcasts a canned message over the mesh.

**Steps:**
1. FAP running, `PROTO:RDY` confirmed
2. Press OK to select a profile
3. Navigate to a message with UP/DOWN
4. Press OK to send
5. Watch Heltec OLED ChUtil and second node's Meshtastic app

**Pass criteria:**
- `Sent: <message>` banner appears briefly on Flipper screen
- ChUtil ticks up on Heltec OLED
- Message appears on second node's Meshtastic app

---

## Test 6: GhostMesh FAP — Receive

**Goal:** Confirm incoming mesh messages appear on the Flipper.

**Steps:**
1. FAP running, `PROTO:RDY` confirmed
2. From the second node, send a text message via the Meshtastic phone app
3. Watch the bottom status bar of the GhostMesh FAP

**Pass criteria:** Status bar shows `<node_id>: <message>` (e.g., `2f74: Hello`).

---

## Test 7: Custom YAML Profile Load

**Goal:** Confirm custom profiles are loaded from SD card.

**Steps:**
1. Copy `examples/profiles.yaml` from the repo to `SD:/apps_data/ghostmesh/profiles.yaml` on the Flipper
2. Launch GhostMesh
3. On the profile selection screen, scroll past the 3 built-ins

**Pass criteria:** Custom profile names from the YAML file appear in the profile list.

---

## Known Failure Modes

| Symptom | Likely Cause | Resolution |
|---------|-------------|-----------|
| `PROTO:...` never becomes `PROTO:RDY` | Meshtastic phone app connected via BLE and consuming PhoneAPI | Close Meshtastic app on phone, restart FAP |
| `PROTO:...` stuck — no handshake | Wire not connected to GPIO44, or bad solder joint | Check Flipper TX → GPIO44 connection |
| Boot log appears (Test 2) but nothing received in FAP | Flipper TX → GPIO44 path broken | Verify GPIO44 wire; try Test 1 loopback first |
| `PROTO:RDY` shows but OK does nothing | Profile screen still selected (need to pick a profile first, then OK sends from message screen) | Select profile with OK, then navigate messages |
| Message not appearing on second node | Nodes out of range, wrong channel, or ChUtil at airtime limit | Bring nodes closer, confirm LongFast channel |
| Custom YAML profiles not showing | File missing, wrong path, or parse error | Confirm path `SD:/apps_data/ghostmesh/profiles.yaml`; check file has valid `name:` and `- ` lines |
