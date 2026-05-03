# Hardware Reference

## Flipper Zero

- **MCU:** STM32WB55 (ARM Cortex-M4 + M0+)
- **Display:** 128×64 monochrome LCD
- **GPIO header:** Available via the official prototype board
- **UART:** USART1 on header pins 13 (TX) / 14 (RX)
- **Firmware:** Stock Flipper firmware or compatible fork (tested with official firmware)

### Prototype Board

The official Flipper Zero prototype board breaks out the GPIO header into a breadboard-friendly form. Required for this project. The relevant pins used:

| Flipper Label | Header Pin | Function |
|---------------|-----------|----------|
| U_TX          | 13        | USART1 TX — data out to Heltec RX |
| U_RX          | 14        | USART1 RX — data in from Heltec TX |
| GND           | 8 or 18   | Ground reference |

---

## MakerHawk ESP32 LoRa V3 (Heltec WiFi LoRa 32 V3 compatible)

- **MCU:** ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **Radio:** SX1262 LoRa transceiver
- **Frequency:** 915 MHz (US/AU band — change via Meshtastic region setting)
- **Antenna:** 915 MHz whip antenna, SMA or IPEX connector
- **USB:** USB-C for flashing and power
- **Battery:** JST connector for LiPo; battery is connected and powers the board independently
- **Display:** 0.96" OLED (128×64), used by Meshtastic firmware

### Heltec UART Pins Used

| Heltec Label | ESP32-S3 Pin | Function |
|-------------|-------------|----------|
| RX (Serial) | GPIO44 (default) | Receives data from Flipper TX |
| TX (Serial) | GPIO43 (default) | Sends data to Flipper RX |
| GND         | GND         | Shared ground |

> **Note:** Meshtastic serial module pin assignments can be customized. The defaults above are for standard Heltec V3 firmware. Verify in the Meshtastic app under Settings → Module Config → Serial if you've changed them.

---

## Communication Chain

```
[Flipper Zero] ──UART 115200──► [Heltec ESP32-S3]
                                        │
                                   [SX1262 LoRa]
                                        │
                               915 MHz mesh radio
                                        │
                              [Other Meshtastic nodes]
```

---

## Power Architecture

```
[Flipper battery]  ──►  [Flipper Zero]  (independent)
[Heltec battery]   ──►  [Heltec board]  (independent)
```

**No power is shared between devices.** Only TX, RX, and GND lines are connected. This is intentional to avoid ground loop issues and to prevent the Flipper from being damaged by USB power differentials when the Heltec is also connected via USB.

---

## Known Working State (as of initial wiring)

- Flipper USB-UART bridge shows byte counters incrementing
- PuTTY on the Flipper COM port receives bytes (display unreadable — expected before Meshtastic serial mode is configured)
- Meshtastic mobile app sees the Heltec node and can send/receive messages
- Physical UART path confirmed alive
