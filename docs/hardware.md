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
| U_TX          | 13        | USART1 TX — data out to Heltec GPIO44 |
| U_RX          | 14        | USART1 RX — data in from Heltec GPIO43 |
| GND           | 8 or 18   | Ground reference |

---

## MakerHawk ESP32 LoRa V3 (Heltec WiFi LoRa 32 V3 compatible)

- **MCU:** ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **Radio:** SX1262 LoRa transceiver
- **Frequency:** 915 MHz (US/AU band — change via Meshtastic region setting)
- **Antenna:** 915 MHz whip antenna, SMA or IPEX connector
- **USB:** USB-C + CP2102 UART bridge chip — powered from Heltec battery (always on)
- **Battery:** JST connector for LiPo; powers the board and the CP2102 independently of USB
- **Display:** 0.96" OLED (128×64), used by Meshtastic firmware

### Heltec UART Pins Used

GhostMesh connects to Meshtastic's **PhoneAPI on UART0**, which is permanently on GPIO43/44 regardless of any Meshtastic serial module settings. These are the pads labeled **TX** and **RX** on the bottom row of the Heltec board.

| Heltec Pad Label | ESP32-S3 GPIO | Direction | Connects to |
|-----------------|--------------|-----------|-------------|
| RX              | GPIO44 (UART0 RX) | Flipper → Heltec | Flipper pin 13 (U_TX) |
| TX              | GPIO43 (UART0 TX) | Heltec → Flipper | Flipper pin 14 (U_RX) |
| GND             | GND           | —         | Flipper GND |

> **Why these specific pads?** UART0 (GPIO43/44) is where Meshtastic's PhoneAPI lives — the same interface used by the phone app and official Python library over USB. Other GPIO pins (GPIO7, GPIO41, etc.) were tested and failed due to I2C conflicts, UART peripheral restrictions, or broken SerialModule PROTO mode in firmware 2.7.x. See [wiring.md](wiring.md) for the full investigation history.

---

## Communication Chain

```
[Flipper Zero]  ──UART 115200──►  [Heltec ESP32-S3 / UART0 / PhoneAPI]
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
[Flipper battery]  ──►  [Flipper Zero]   (independent)
[Heltec battery]   ──►  [Heltec board]   (independent, also powers CP2102)
```

No power is shared between devices. Only TX, RX, and GND are connected. The CP2102 on the Heltec is powered from the Heltec battery — this is fine because when no USB host is connected, the CP2102 TX line is idle (high impedance does not conflict with Flipper TX on GPIO44).

---

## Confirmed Working State

- GhostMesh FAP shows `PROTO:RDY` after a few seconds startup handshake
- OK button sends selected canned message over LoRa mesh
- Incoming text messages from other nodes appear in the GhostMesh status bar
- Two-node end-to-end confirmed: Flipper → f69c → LoRa → 2f74 (TX and RX both working)
