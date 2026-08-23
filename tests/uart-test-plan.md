# GhostMesh Test Plan

## Purpose

Validate the hardware path and PROTO protocol connection between the Flipper Zero and the Heltec ESP32 LoRa V3 before deploying or troubleshooting the GhostMesh FAP.

---

## Confirmed Working Configuration

Verified on battery 2026-07-01.

| Parameter | Value |
|-----------|-------|
| Flipper TX wire | Pin 13 (U_TX) → Heltec **GPIO7** (Serial module RX) |
| Flipper RX wire | Pin 14 (U_RX) → Heltec **GPIO6** (Serial module TX) |
| GND | Flipper GND → Heltec GND |
| Protocol | Meshtastic PROTO via the **Serial module** (StreamAPI) |
| Baud | 115200 |
| Meshtastic serial module | **Required** — enabled, mode PROTO, RX 7, TX 6, 115200, override-console OFF |

> GhostMesh uses the Serial module in PROTO mode on GPIO7/6 — **not** UART0/GPIO43-44. The CP2102 USB bridge clamps 43/44 on battery, so that path only worked on USB power. Configure the Serial module (best over the web client on USB — Bluetooth config screens time out).

---

## Pre-Test Checklist

- [ ] Flipper U_TX (pin 13) → Heltec **GPIO7** (Serial module RX)
- [ ] Flipper U_RX (pin 14) → Heltec **GPIO6** (Serial module TX)
- [ ] GND connected between both boards
- [ ] No 5V or 3.3V shared between boards
- [ ] **Serial module configured**: enabled, PROTO, RX 7, TX 6, 115200, override-console OFF
- [ ] Heltec powered from its own battery (USB out — the deployable case)
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

**Goal:** Confirm the GPIO6 (Serial module TX) → Flipper RX → PuTTY path is alive.

**Steps:**
1. Reconnect wires (Flipper 13 → GPIO7, Flipper 14 → GPIO6)
2. Enable PuTTY session logging: **Session → Logging → All session output → <code>log.txt</code>**
3. Connect PuTTY to the Flipper COM port at 115200
4. Unplug and replug the Heltec battery while PuTTY is connected
5. Open <code>log.txt</code>

**Pass criteria:** ESP32 boot ROM messages and Meshtastic startup log appear in the file. This confirms the GPIO6 (Serial module TX) → Flipper RX direction is working. (Note: raw ESP boot-ROM text comes out on UART0/GPIO43 regardless of config; the *Meshtastic* PROTO stream is what should be on GPIO6.)

---

## Test 3: End-to-End PROTO Send (Python)

**Goal:** Confirm the full handshake and text send chain without the FAP.

**Requirements:** <code>pip install pyserial</code> on Windows. Flipper USB-UART Bridge running on Flipper.

**Steps:**
1. From Windows, run:
   ```
   python S:\path\to\ghostmesh\tests\proto_send_test.py COM3
   ```
2. Watch the Heltec OLED for ChUtil change
3. Check the second node's Meshtastic app

**Pass criteria:**
- Script prints <code>config_complete_id received — ready!</code> (PROTO handshake complete)
- ChUtil ticks up (LoRa transmitted)
- "TEST FROM PYTHON" appears on second node

---

## Test 4: GhostMesh FAP — Startup and Handshake

**Goal:** Confirm the FAP connects via PROTO on startup.

**Steps:**
1. Close USB-UART Bridge if running (releases USART1)
2. Launch GhostMesh: **Apps → Tools → GhostMesh**
3. Watch top-right of screen for a few seconds

**Pass criteria:** Screen shows <code>PROTO:...</code> briefly then changes to <code>PROTO:RDY</code>. Profile selection screen appears.

---

## Test 5: GhostMesh FAP — Send via OK Button

**Goal:** Confirm pressing OK broadcasts a canned message over the mesh.

**Steps:**
1. FAP running, <code>PROTO:RDY</code> confirmed
2. Press OK to select a profile
3. Navigate to a message with UP/DOWN
4. Press OK to send
5. Watch Heltec OLED ChUtil and second node's Meshtastic app

**Pass criteria:**
- <code>Sent: <message></code> banner appears briefly on Flipper screen
- ChUtil ticks up on Heltec OLED
- Message appears on second node's Meshtastic app

---

## Test 6: GhostMesh FAP — Receive

**Goal:** Confirm incoming mesh messages appear on the Flipper.

**Steps:**
1. FAP running, <code>PROTO:RDY</code> confirmed
2. From the second node, send a text message via the Meshtastic phone app
3. Watch the bottom status bar of the GhostMesh FAP

**Pass criteria:** Status bar shows <code><node_id>: <message></code> (e.g., <code>2f74: Hello</code>).

---

## Test 7: Custom YAML Profile Load

**Goal:** Confirm custom profiles are loaded from SD card.

**Steps:**
1. Copy <code>examples/profiles.yaml</code> from the repo to <code>SD:/apps_data/ghostmesh/profiles.yaml</code> on the Flipper
2. Launch GhostMesh
3. On the profile selection screen, scroll past the 3 built-ins

**Pass criteria:** Custom profile names from the YAML file appear in the profile list.

---

## Known Failure Modes

| Symptom | Likely Cause | Resolution |
|---------|-------------|-----------|
| <code>PROTO:...</code> connects only when the Heltec is on **USB power** | Wired to GPIO43/44 — the CP2102 clamps those on battery | **Move the Heltec-side wires to GPIO7 (RX) / GPIO6 (TX)** and set the Serial module to PROTO on 7/6 |
| <code>PROTO:...</code> never becomes <code>PROTO:RDY</code> on battery | Serial module off / wrong mode / wrong pins | Web client over USB → Serial: enabled, PROTO, RX 7, TX 6, 115200, override-console OFF |
| <code>PROTO:...</code> never becomes <code>PROTO:RDY</code> | Meshtastic phone app connected via BLE and holding the StreamAPI | Disconnect the Meshtastic app / turn off phone BLE, restart FAP |
| <code>PROTO:...</code> stuck — no handshake | Wire not connected to GPIO7, or bad solder joint | Check Flipper TX (pin 13) → GPIO7 connection |
| Boot log appears (Test 2) but nothing received in FAP | Flipper TX → GPIO7 path broken, or Serial module not on 7/6 | Verify GPIO7 wire and Serial-module pins; try Test 1 loopback first |
| Isolating node vs Flipper | — | Web client over the Heltec's own USB connects but the FAP won't = CP2102 clamp / wire; move to 6/7 |
| <code>PROTO:RDY</code> shows but OK does nothing | Profile screen still selected (need to pick a profile first, then OK sends from message screen) | Select profile with OK, then navigate messages |
| Message not appearing on second node | Nodes out of range, wrong channel, or ChUtil at airtime limit | Bring nodes closer, confirm LongFast channel |
| Custom YAML profiles not showing | File missing, wrong path, or parse error | Confirm path <code>SD:/apps_data/ghostmesh/profiles.yaml</code>; check file has valid <code>name:</code> and <code>- </code> lines |
