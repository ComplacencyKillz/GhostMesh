# Meshtastic Setup

## Prerequisites

- Heltec WiFi LoRa 32 V3 (or MakerHawk ESP32 LoRa V3 compatible) flashed with Meshtastic firmware
- Meshtastic mobile app (iOS or Android) connected to the node via Bluetooth
- Antenna attached before powering the radio
- Node confirmed working (visible in the app, able to send/receive messages)

---

## Flash Meshtastic (if not already done)

1. Download the Meshtastic flasher: https://flasher.meshtastic.org
2. Connect the Heltec board via USB-C
3. Select **Heltec WiFi LoRa 32 V3** as the target device
4. Flash the latest stable firmware
5. After flash: open the Meshtastic app, connect via Bluetooth, complete initial setup (region, name)

---

## Configure Serial Module for GhostMesh

### In the Meshtastic App

1. Connect to your node via Bluetooth in the Meshtastic app
2. Go to: **Settings (gear icon) → Module Config → Serial**
3. Set the following:

| Setting | Value |
|---------|-------|
| Enabled | On |
| Echo | Off |
| Mode | **DEFAULT** (this is PROTO mode) |
| Baud Rate | **115200** |
| Timeout | 0 (default) |
| RX | **7** (Flipper TX wire — GPIO7 on top row) |
| TX | **43** (Flipper RX wire — GPIO43 "TX" pad on bottom row) |
| Override console serial port | Off |

4. Tap **Save**
5. The node will reboot. Reconnect via Bluetooth and confirm the Serial module shows Enabled.

### Verify via Python (recommended over PuTTY)

On Windows with the Flipper USB-UART Bridge running:

```python
python -c "import serial,time; s=serial.Serial('COM3',115200,timeout=2); time.sleep(1); s.write(b'CHECKIN OK\n'); print('sent'); print(s.read(64)); s.close()"
```

Watch the Heltec OLED — **ChUtil should increase from 0% to ~6%** confirming the LoRa radio transmitted. With a second Meshtastic node in range, "CHECKIN OK" will appear in the Meshtastic app on the receiving device.

> Note: Messages sent via the serial module may not appear in the sending node's own Meshtastic
> app. ChUtil increasing is the correct single-node confirmation of a successful transmission.

---

## Region Setting

Confirm your region is set to **US** (or appropriate for your country) under:
**Settings → Radio Config → LoRa → Region**

The 915 MHz antenna included with the MakerHawk/Heltec V3 is tuned for the US ISM band (902–928 MHz). Using a mismatched region risks poor RF performance.

---

## Confirming the Node is Ready

- Node appears in Meshtastic app with name and battery level
- Node can receive a test message sent from the app
- Serial module shows Enabled: On in Module Config
- When you send a message from the app, PuTTY (or the GhostMesh FAP) should show bytes incrementing

---

## Known Hardware Notes (Heltec V3 + Meshtastic 2.7.x)

**Do not use the pad labeled "RX" (GPIO44) for the Meshtastic serial RX pin.** GPIO44 is UART0 RX on the ESP32-S3 and is claimed at boot. Meshtastic's serial module (UART1) cannot receive on it — bytes arrive at the GPIO but the interrupt never fires. Use **GPIO7** instead.

**Do not use GPIO41 or GPIO42.** These are claimed by Meshtastic's I2C bus 2 (`sda=41, scl=42`) at firmware init.

**"Override console serial port"** is only available in NMEA and CalTopo modes. It is not an option for TEXTMSG.

**LOGTEXT boot output** only streams on power-on. After the node has finished booting, LOGTEXT goes quiet unless events occur. Connect PuTTY before powering the node to capture boot logs.
