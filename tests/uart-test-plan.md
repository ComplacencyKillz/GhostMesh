# UART Test Plan

## Purpose

Validate the physical UART connection between the Flipper Zero and the Heltec ESP32 LoRa V3 before and after deploying the GhostMesh FAP.

---

## Pre-Test Checklist

- [ ] Flipper U_TX (pin 13) connected to Heltec RX
- [ ] Flipper U_RX (pin 14) connected to Heltec TX
- [ ] GND connected between both boards
- [ ] No 5V or 3.3V shared between boards
- [ ] Heltec powered from its own battery
- [ ] Flipper powered from its own battery
- [ ] Meshtastic serial module: Enabled, Mode = TEXTMSG, Baud = 115200
- [ ] Heltec is not connected to USB during UART testing (USB can interfere with serial)

---

## Test 1: Physical Path Validation (USB-UART Bridge)

**Goal:** Confirm bytes flow between Flipper and Heltec before deploying the FAP.

**Steps:**
1. On the Flipper: **Apps → GPIO → USB-UART Bridge**
2. Select USART at 115200 baud
3. Connect PuTTY to the Flipper COM port at 115200 8N1
4. Open the Meshtastic app on your phone
5. Send a test message from the Meshtastic app: "Hello GhostMesh"

**Expected result:**
- The Flipper USB-UART Bridge screen shows RX byte counter increasing
- PuTTY shows the message text (if TEXTMSG mode is set) or binary data (if still in DEFAULT/PROTO mode)

**Pass criteria:** RX byte counter increases on any message sent from the Meshtastic app.

---

## Test 2: TX Path Validation (Type in PuTTY)

**Goal:** Confirm the Flipper can send bytes to the Heltec.

**Steps:**
1. With USB-UART Bridge still running and PuTTY connected
2. Type `HELLO FROM FLIPPER` in PuTTY and press Enter

**Expected result:**
- TX byte counter on the Flipper increases
- In the Meshtastic app on your phone, a message `HELLO FROM FLIPPER` appears from the node

**Pass criteria:** Message appears in Meshtastic app. TX counter increments.

---

## Test 3: GhostMesh FAP — UART ACTIVE Status

**Goal:** Confirm the FAP successfully acquires UART on startup.

**Steps:**
1. Exit USB-UART Bridge on the Flipper (important: release UART before launching FAP)
2. Launch GhostMesh FAP: **Apps → Tools → GhostMesh**

**Expected result:**
- Display shows `UART: ACTIVE`
- No `UART: ERROR` message

**Pass criteria:** UART: ACTIVE displayed within 1 second of launch.

---

## Test 4: GhostMesh FAP — RX Counter

**Goal:** Confirm incoming bytes from Heltec increment the RX counter.

**Steps:**
1. GhostMesh FAP running, UART: ACTIVE confirmed
2. Send a message from the Meshtastic mobile app

**Expected result:**
- RX byte counter on the Flipper display increases after each message
- Counter value is non-zero and increases monotonically

**Pass criteria:** RX counter increments by approximately the number of bytes in the received message + newline.

---

## Test 5: GhostMesh FAP — TX Send

**Goal:** Confirm pressing OK sends `CHECKIN OK` over UART and it appears in Meshtastic.

**Steps:**
1. GhostMesh FAP running
2. Press OK on the Flipper

**Expected result:**
- TX byte counter increases by 11 (len("CHECKIN OK\n") = 11)
- In the Meshtastic mobile app, a message `CHECKIN OK` appears from the node

**Pass criteria:** Message appears in Meshtastic app. TX counter increments by 11.

---

## Test 6: Exit and Resource Release

**Goal:** Confirm the FAP releases UART cleanly so other apps can use it afterwards.

**Steps:**
1. Press BACK in the GhostMesh FAP to exit
2. Launch USB-UART Bridge: **Apps → GPIO → USB-UART Bridge**

**Expected result:**
- USB-UART Bridge starts without error
- Byte counters continue working normally

**Pass criteria:** No crash, hang, or "UART busy" error on USB-UART Bridge launch after GhostMesh exits.

---

## Known Failure Modes

| Symptom | Cause | Resolution |
|---------|-------|-----------|
| UART: ERROR on startup | Another app still holds USART1 | Exit USB-UART Bridge before launching GhostMesh |
| RX counter stuck at 0 | TX/RX wires swapped | Swap the TX/RX connections |
| RX counter increments but TX has no effect | GND missing | Confirm GND wire is connected |
| Message not appearing in Meshtastic app | Serial module not in TEXTMSG mode | Reconfigure in Meshtastic app settings |
| Meshtastic app shows binary garbage messages | Serial mode set to PROTO/DEFAULT | Change to TEXTMSG in Meshtastic serial settings |
