# Wiring Guide

## Overview

The Flipper Zero connects to the Heltec ESP32 LoRa V3 board over **3 wires only**: TX, RX, and GND. No power is shared between the devices.

---

## Connection Table

| Signal | Flipper Proto Board | Heltec ESP32 LoRa V3 |
|--------|--------------------|-----------------------|
| UART TX (Flipper → Heltec) | **U_TX / GPIO pin 13** | **GPIO7** (top row, labeled "7") |
| UART RX (Heltec → Flipper) | **U_RX / GPIO pin 14** | **TX pad** (bottom row, labeled "TX" = GPIO43) |
| Ground reference | **GND** | **GND** |
| Power | — NOT CONNECTED — | — NOT CONNECTED — |

> **Why GPIO7 for RX and not the pad labeled "RX" (GPIO44)?**
> GPIO44 is UART0 RX on the ESP32-S3 and is owned by the ESP32 boot ROM and Meshtastic console.
> GPIO41/42 are claimed by Meshtastic's I2C bus 2 at boot. GPIO7 is confirmed free — not used
> by UART0, I2C, SPI (LoRa), USB, or ADC. UART1 can use it cleanly for RX. The pad labeled "TX"
> (GPIO43) works fine for UART1 transmit because output can be routed to that pin without conflict.

---

## Wiring Diagram (ASCII)

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│       Flipper Zero           │         │   Heltec WiFi LoRa 32 V3     │
│    (via Prototype Board)     │         │     (Meshtastic firmware)     │
│                              │         │                              │
│  U_TX / GPIO 13  ───────────────────►  GPIO7  (top row, "7" pad)    │
│  U_RX / GPIO 14  ◄───────────────────  GPIO43  (bottom row, "TX" pad)│
│  GND             ───────────────────── GND                           │
│                              │         │                              │
│  [5V  → NOT connected]       │         │  [VCC → own battery]        │
│  [3V3 → NOT connected]       │         │                              │
└─────────────────────────────┘         └──────────────────────────────┘

[Flipper battery]  powers Flipper
[Heltec battery]   powers Heltec
```

---

## Safety Rules

1. **Do NOT connect 5V or 3.3V rails between the devices** — each device runs from its own battery. Sharing power risks damaging the Flipper's GPIO or the ESP32.

2. **Do NOT connect power while either device is powered via USB** — if the Heltec is plugged into USB during development, keep the Heltec USB connected only to the computer, not to the Flipper.

3. **GND must be shared** — the TX/RX signals are referenced to ground. Without a shared GND, UART will not work.

4. **3.3V logic levels** — the Flipper GPIO and the Heltec ESP32 both operate at 3.3V logic. No level shifter is needed.

---

## Flipper Prototype Board Pinout Reference

```
Flipper GPIO Header (top view, pin 1 on left):

  1  2  3  4  5  6  7  8
  ●  ●  ●  ●  ●  ●  ●  ●
  5V 3V  PC  PC  PA  PA  PA  GND
     3   B3  B2  7   6   4

  9  10  11  12  13  14  15  16  17  18
  ●   ●   ●   ●   ●   ●   ●   ●   ●   ●
  PC  PC  PC  PA  PA  PA  PB  PB  PC  GND
  1   0   3   0   9   10  2   3   8
```

- **Pin 13 = PA9 = USART1_TX** → connect to Heltec **GPIO7** (top row "7" pad)
- **Pin 14 = PA10 = USART1_RX** → connect to Heltec **GPIO43** (bottom row "TX" pad)
- **Pin 8 or 18 = GND** → connect to Heltec GND

---

## Confirmed GPIO Pin Status (Heltec V3, Meshtastic 2.7.x)

Determined from Meshtastic 2.7.15 boot log analysis:

| GPIO | Status | Claimed by |
|------|--------|-----------|
| 43 | USED — safe for UART1 TX output | UART0 TX (shared output works) |
| 44 | UNAVAILABLE for UART1 RX | UART0 RX — conflict kills receive |
| 41 | UNAVAILABLE | I2C bus 2 SDA (`i2cInit: sda=41 scl=42`) |
| 42 | UNAVAILABLE | I2C bus 2 SCL |
| 17 | UNAVAILABLE | I2C bus 1 SDA (OLED) |
| 18 | UNAVAILABLE | I2C bus 1 SCL (OLED) |
| 8–14 | UNAVAILABLE | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 19 | UNAVAILABLE | USB D- (ESP32-S3 native USB) |
| 20 | UNAVAILABLE | USB D+ |
| 1 | UNAVAILABLE | Battery ADC |
| **7** | **FREE — confirmed working** | Nothing — UART1 RX works cleanly |

---

## End-to-End Verification

Verified 2026-05-03 using Python `pyserial` on Windows:

```python
import serial, time
s = serial.Serial('COM3', 115200, timeout=2)
time.sleep(1)
s.write(b'CHECKIN OK\n')
s.close()
```

- Heltec OLED **ChUtil** increased from 0% → 6% confirming LoRa radio transmitted
- Echo bytes returned confirming GPIO7 RX → Meshtastic serial module → GPIO43 TX path alive
- Flipper TX loopback (pin 13 → pin 14) confirmed Flipper UART bridge works in both directions

**To fully verify mesh delivery end-to-end**, a second Meshtastic node (any Heltec V3 + battery + 915 MHz antenna + phone) will receive "CHECKIN OK" in the Meshtastic app Messages view when the Python script or FAP sends it.

---

## Baud Rate

Default: **115200 baud, 8N1**

This matches the Meshtastic serial module default. If you change it on either side, update both `GHOSTMESH_UART_BAUD` in `uart_helper.h` and the Meshtastic serial module baud rate setting.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No bytes received by Flipper | TX/RX reversed | Swap the TX and RX wires |
| Byte counts increase but all garbage | Baud mismatch | Verify both sides at 115200 |
| No data at all | Missing GND | Confirm GND wire is connected |
| Heltec not responding | Serial module disabled | Enable serial in Meshtastic app |
| Flipper app shows UART ERROR | UART already acquired | Ensure no other Flipper app is using USART1 |
