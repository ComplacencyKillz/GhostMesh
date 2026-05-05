# UART Test Plan

## Purpose

Validate the physical UART connection between the Flipper Zero and the Heltec ESP32 LoRa V3 before and after deploying the GhostMesh FAP.

---

## Confirmed Working Configuration

Verified 2026-05-03. Use these exact settings:

| Parameter | Value |
|-----------|-------|
| Flipper TX wire | Pin 13 (U_TX) → Heltec **GPIO7** (top row, "7" pad) |
| Flipper RX wire | Pin 14 (U_RX) → Heltec **GPIO43** (bottom row, "TX" pad) |
| GND | Flipper GND → Heltec GND |
| Meshtastic serial RX pin | **7** |
| Meshtastic serial TX pin | **43** |
| Baud | 115200 |
| Mode | TEXTMSG |
| Echo | Off |

> Do NOT use the Heltec pad labeled "RX" (GPIO44) — it conflicts with UART0 and serial receive
> silently fails. Do NOT use GPIO41/42 — claimed by I2C at boot. GPIO7 is confirmed free.

---

## Pre-Test Checklist

- [ ] Flipper U_TX (pin 13) → Heltec **GPIO7** (top row "7" pad)
- [ ] Flipper U_RX (pin 14) → Heltec **GPIO43** (bottom row "TX" pad)
- [ ] GND connected between both boards
- [ ] No 5V or 3.3V shared between boards
- [ ] Heltec powered from its own battery
- [ ] Flipper powered from its own battery
- [ ] Meshtastic: Serial enabled, RX=7, TX=43, Mode=TEXTMSG, Baud=115200, Echo=Off
- [ ] Heltec not connected to USB during UART testing

---

## Test 1: Flipper TX Loopback

**Goal:** Confirm Flipper UART bridge transmits on pin 13 before involving the Heltec.

**Steps:**
1. Disconnect TX/RX wires from Heltec
2. Jumper Flipper pin 13 directly to Flipper pin 14
3. Open USB-UART Bridge on Flipper at 115200
4. Open PuTTY, set local echo OFF (Terminal → Local echo → Force off)
5. Type any character

**Pass criteria:** Character appears on PuTTY screen (traveled out pin 13, back in pin 14).

---

## Test 2: Heltec TX → Flipper RX (boot log)

**Goal:** Confirm Heltec GPIO43 TX → Flipper RX → PuTTY path is alive.

**Steps:**
1. Reconnect wires (GPIO7 and GPIO43)
2. Set Meshtastic serial mode to **LOGTEXT**, enable PuTTY session logging to file
3. Unplug and replug Heltec battery while PuTTY is connected
4. Open the log file

**Pass criteria:** ESP32 boot ROM messages and Meshtastic startup log appear in the file.

---

## Test 3: Flipper TX → Heltec GPIO7 RX (echo)

**Goal:** Confirm bytes from Flipper actually reach Meshtastic's serial module on GPIO7.

**Steps:**
1. Set Meshtastic serial mode to **LOGTEXT**, echo **ON**
2. Enable PuTTY session logging to file
3. Power cycle Heltec, wait for boot to finish (~5 seconds)
4. Type `HELLO` + Enter in PuTTY
5. Open the log file

**Pass criteria:** `HELLO` appears in the log file (echoed back via GPIO43). Boot log present confirms GPIO43 TX works. HELLO present confirms GPIO7 RX works.

---

## Test 4: End-to-End TEXTMSG Send (Python)

**Goal:** Confirm serial → Meshtastic → LoRa mesh transmission chain.

**Steps:**
1. Set Meshtastic mode back to **TEXTMSG**, echo Off
2. On Windows PC: `python -c "import serial,time; s=serial.Serial('COM3',115200,timeout=2); time.sleep(1); s.write(b'CHECKIN OK\n'); print('sent'); print(s.read(64)); s.close()"`
3. Watch Heltec OLED display

**Pass criteria:** **ChUtil percentage increases** (0% → any non-zero value) confirming LoRa radio transmitted the mesh packet.

> Note: The message may not appear in the Meshtastic phone app when only one node is present
> (no other node to receive and display it). ChUtil increase is the single-node confirmation.
> For full end-to-end receipt confirmation, use a second Meshtastic node — see Test 5.

---

## Test 5: Second Node Receipt Confirmation (optional but recommended)

**Goal:** Confirm the transmitted mesh packet is correctly received by another node.

**Requirements:** Second Meshtastic node (any board) + 915 MHz antenna + battery, within ~50 feet.

**Steps:**
1. Flash second node with Meshtastic, set region US, default LongFast channel
2. Power it on near the first Heltec
3. Run the Python send script or press OK on the GhostMesh FAP
4. Open Meshtastic app connected to (or watching) the second node

**Pass criteria:** "CHECKIN OK" appears as a received message on the second node's app.

**Status: PASSED 2026-05-05** — two Heltec V3 nodes confirmed communicating. Send from PuTTY via node 1, received on node 2 app. Receive direction also confirmed (node 2 → node 1 serial output visible in PuTTY).

---

## Test 6: GhostMesh FAP — UART ACTIVE

**Goal:** Confirm the FAP acquires UART on startup.

**Steps:**
1. Exit USB-UART Bridge (releases USART1)
2. Launch GhostMesh: **Apps → Tools → GhostMesh**

**Pass criteria:** Display shows `UART: ACTIVE` within 1 second.

---

## Test 7: GhostMesh FAP — Send via OK Button

**Goal:** Confirm pressing OK sends CHECKIN OK over the mesh.

**Steps:**
1. GhostMesh FAP running, UART: ACTIVE confirmed
2. Press OK on the Flipper
3. Watch Heltec OLED ChUtil

**Pass criteria:** TX byte counter increments by 11, ChUtil increases on Heltec.

---

## Known Failure Modes

| Symptom | Cause | Resolution |
|---------|-------|-----------|
| UART: ERROR on FAP startup | USB-UART Bridge still running | Exit bridge before launching FAP |
| Boot log appears but HELLO not echoed | GPIO7 solder joint bad | Reflow GPIO7 pad |
| Nothing at all from Heltec | Wrong TX pin or GND missing | Verify GPIO43 wire and GND |
| ChUtil does not increase | Serial module not initialized | Check RX=7, TX=43 saved in Meshtastic |
| PuTTY loopback fails | Flipper UART Bridge not transmitting | Restart bridge app, check pin 13 wire |
| Second node doesn't receive | Nodes too far apart or wrong channel | Bring nodes within 10 feet, confirm same channel/region |
