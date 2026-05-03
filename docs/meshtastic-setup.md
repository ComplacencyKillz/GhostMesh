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
| Echo | Off (unless debugging) |
| Mode | **TEXTMSG** |
| Baud Rate | **115200** |
| Timeout | 0 (default) |
| RX | Leave default (or set to your Heltec RX GPIO) |
| TX | Leave default (or set to your Heltec TX GPIO) |

4. Tap **Save**
5. The node will reboot. Reconnect via Bluetooth and confirm the Serial module shows Enabled.

### Verify via Serial Terminal (optional)

With the Heltec connected to your computer via USB and PuTTY open on the Flipper COM port:

1. Send a message from the Meshtastic app (phone → mesh)
2. If TEXTMSG mode is working, you should see the message appear as a plain text line in PuTTY
3. If you type a line in PuTTY and press Enter, it should appear as a mesh message in the Meshtastic app

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

## Known Issue: Unreadable PuTTY Output

Before configuring TEXTMSG mode, PuTTY connected to the Flipper USB-UART bridge will show unreadable binary output. This is normal — the Meshtastic node outputs protobuf data in DEFAULT mode. Switching to TEXTMSG makes the output human-readable plain text.

Byte counters increasing = physical UART path is alive. Unreadable bytes = wrong serial mode. This is an expected state before TEXTMSG is configured.
