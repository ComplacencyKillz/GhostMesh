# Wiring Guide

## Overview

The Flipper Zero connects to the Heltec ESP32 LoRa V3 board over **3 wires only**: TX, RX, and GND. No power is shared between the devices.

---

## Connection Table

| Signal | Flipper Proto Board | Heltec ESP32 LoRa V3 |
|--------|--------------------|-----------------------|
| UART TX (Flipper → Heltec) | **U_TX / GPIO pin 13** | **GPIO44** (bottom row, labeled "RX") |
| UART RX (Heltec → Flipper) | **U_RX / GPIO pin 14** | **GPIO43** (bottom row, labeled "TX") |
| Ground reference | **GND** | **GND** |
| Power | — NOT CONNECTED — | — NOT CONNECTED — |

> **Why GPIO7 for RX and not the pad labeled "RX" (GPIO44)?**
> GhostMesh uses Meshtastic's PhoneAPI which lives permanently on UART0 (GPIO43/44).
> This is the same interface used by the official meshtastic Python library and the phone app
> via the CP2102 USB bridge. Connecting the Flipper directly to GPIO43/44 accesses the PhoneAPI
> without any special Meshtastic serial module configuration.
> GPIO44 = UART0 RX (Flipper TX connects here). GPIO43 = UART0 TX (Flipper RX connects here).
> The labeled "RX" and "TX" pads on the Heltec board are exactly these pins.

---

## Wiring Diagram (ASCII)

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│       Flipper Zero           │         │   Heltec WiFi LoRa 32 V3     │
│    (via Prototype Board)     │         │     (Meshtastic firmware)     │
│                              │         │                              │
│  U_TX / GPIO 13  ───────────────────►  GPIO44  (bottom row, "RX" pad)│
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

- **Pin 13 = PA9 = USART1_TX** → connect to Heltec **GPIO44** (bottom row "RX" labeled pad)
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

## Sensor Wiring (Phases 7–11)

All sensor connections are independent of the Flipper–Heltec UART link.
Wire sensors after the base UART connection is confirmed working.

**Architecture:** The Heltec backpack operates fully unattended. Sensors that must
function without the Flipper present are wired to the Heltec and driven by custom
Meshtastic modules. The Flipper ProtoBoard carries only operator controls.

### Heltec I2C Sensor Bus (GPIO41 SDA / GPIO42 SCL)

Connect the STEMMA QT 5-port passive hub to the Heltec GPIO41/42 header pins.
Then daisy-chain sensors via Qwiic cables from the hub.

```
Heltec GPIO41 (SDA) ──► STEMMA QT hub port 1 (any port)
Heltec GPIO42 (SCL) ──► STEMMA QT hub port 1

Hub port 2 ──► BME280 Qwiic in
Hub port 3 ──► MAX17048 Qwiic in

MAX17048 JST-PH 2-pin ──► Heltec battery JST-PH 2-pin
                           (T-junction or dedicated tap — do not disconnect Heltec battery)
```

**I2C addresses:** BME280=0x76, MAX17048=0x36, OLED=0x3C (bus 1 only). No conflicts.

Do NOT connect sensors to GPIO17/18 — that is the OLED bus and is hardwired to the board.

### BN-220 GPS (Heltec UART1 — GPIO35/36)

```
BN-220 TX  ──► Heltec GPIO35   (UART1 RX)
BN-220 RX  ──► Heltec GPIO36   (UART1 TX — optional, only needed to reconfigure GPS)
BN-220 VCC ──► Heltec 3.3V rail via GPIO26 Vext enable
BN-220 GND ──► Heltec GND
```

GPIO26 (Vext) controls the Heltec's external 3.3V rail. Drive GPIO26 HIGH in firmware to
power the GPS. Leave LOW when GPS is not needed to save battery. Cold-start acquisition:
30–90 seconds. Always-on draws 20–40mA.

### HC-SR04 Ultrasonic (Heltec GPIO21/47)

```
HC-SR04 Trig ──► Heltec GPIO21
HC-SR04 Echo ──► Heltec GPIO47
HC-SR04 VCC  ──► 5V (from USB pin when USB-powered) or 3.3V (verify your module)
HC-SR04 GND  ──► Heltec GND
```

**Voltage note:** Standard HC-SR04 requires 5V for full 4m range. The Heltec 5V pin is
only live when USB is connected. If operating on battery only, use a 3.3V-tolerant clone
(verify the data sheet for your specific module) or add a small boost converter.
Verify GPIO21 is free on your board revision before soldering.

### Photoresistor / Light Tamper (Heltec GPIO5 ADC)

```
Photoresistor leg 1 ──► Heltec 3.3V
Photoresistor leg 2 ──► Heltec GPIO5  AND  10kΩ resistor to GND (voltage divider)
```

GPIO5 is ADC1_CH4 on ESP32-S3. Do not use GPIO1 — it is reserved for the battery ADC.

### IR Receiver (Heltec GPIO48)

```
IR module signal pin ──► Heltec GPIO48
IR module VCC        ──► Heltec 3.3V
IR module GND        ──► Heltec GND
```

Standard NEC protocol receiver (3-pin module from Elegoo kit). Active-low output.

### Heltec — Tamper & Control Sensors

```
SW-520D tilt switch (tamper — backpack moved/disturbed):
  Pin 1 ──► Heltec GPIO2
  Pin 2 ──► Heltec 3.3V
  Add 10kΩ pull-down from GPIO2 to GND.

Slide switch (physical arm/disarm on deployment):
  Common ──► Heltec GPIO4
  NO     ──► Heltec 3.3V
  NC     ──► GND (or leave open)
  Add 10kΩ pull-down from GPIO4 to GND.

IR receiver module (remote arm/disarm ~10m via NEC remote):
  Signal ──► Heltec GPIO48
  VCC    ──► Heltec 3.3V
  GND    ──► Heltec GND
  Active-low output; module includes internal pull-up.
```

### Flipper ProtoBoard — Operator Controls

```
Slide switch (operator arming gate — gates nuke and destructive actions):
  Common ──► Flipper pin 15 (PB2)
  NO     ──► Flipper 3.3V (pin 2)
  NC     ──► GND (or leave open)
  Add 10kΩ pull-down from pin 15 to GND.

Active buzzer (audible alert — incoming messages, relayed tamper events):
  Flipper pin 5 (PA7) ──► 1kΩ ──► PN2222 base
  PN2222 collector    ──► Buzzer negative terminal
  PN2222 emitter      ──► GND
  Buzzer positive     ──► Flipper 3.3V (pin 2)

Coin vibration motor (haptic alert — incoming messages):
  Flipper pin 6 (PA6) ──► 100Ω ──► AO3400 gate
  AO3400 drain        ──► Motor negative terminal
  AO3400 source       ──► GND
  Motor positive      ──► Flipper 3.3V (pin 2)
  1N4007 anode        ──► Motor negative (drain side)
  1N4007 cathode      ──► Motor positive (3.3V side)
  The 1N4007 flyback diode is mandatory — without it the motor's back-EMF
  spike will damage the MOSFET and potentially the Flipper GPIO.
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No bytes received by Flipper | TX/RX reversed | Swap the TX and RX wires |
| Byte counts increase but all garbage | Baud mismatch | Verify both sides at 115200 |
| No data at all | Missing GND | Confirm GND wire is connected |
| Heltec not responding | Serial module disabled | Enable serial in Meshtastic app |
| Flipper app shows UART ERROR | UART already acquired | Ensure no other Flipper app is using USART1 |
