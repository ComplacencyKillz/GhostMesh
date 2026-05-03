# Wiring Guide

## Overview

The Flipper Zero connects to the Heltec ESP32 LoRa V3 board over **3 wires only**: TX, RX, and GND. No power is shared between the devices.

---

## Connection Table

| Signal | Flipper Proto Board | Heltec ESP32 LoRa V3 |
|--------|--------------------|-----------------------|
| UART TX (Flipper → Heltec) | **U_TX / GPIO pin 13** | **RX pin** |
| UART RX (Heltec → Flipper) | **U_RX / GPIO pin 14** | **TX pin** |
| Ground reference | **GND** | **GND** |
| Power | — NOT CONNECTED — | — NOT CONNECTED — |

> **TX/RX cross:** Flipper TX goes to Heltec RX. Flipper RX comes from Heltec TX. This is standard UART crossover and is correct.

---

## Wiring Diagram (ASCII)

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│       Flipper Zero           │         │   Heltec WiFi LoRa 32 V3     │
│    (via Prototype Board)     │         │     (Meshtastic firmware)     │
│                              │         │                              │
│  U_TX / GPIO 13  ───────────────────►  RX                            │
│  U_RX / GPIO 14  ◄───────────────────  TX                            │
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

- **Pin 13 = PA9 = USART1_TX** → connect to Heltec RX
- **Pin 14 = PA10 = USART1_RX** → connect to Heltec TX
- **Pin 8 or 18 = GND** → connect to Heltec GND

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
