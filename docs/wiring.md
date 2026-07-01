# Wiring Guide

## Overview

The Flipper Zero connects to the Heltec ESP32 LoRa V3 board over **3 wires only**: TX, RX, and GND. No power is shared between the devices.

---

## Connection Table

| Signal | Flipper Proto Board | Heltec ESP32 LoRa V3 |
|--------|--------------------|-----------------------|
| UART TX (Flipper → Heltec) | **U_TX / GPIO pin 13** | **GPIO7** (Serial module RX) |
| UART RX (Heltec → Flipper) | **U_RX / GPIO pin 14** | **GPIO6** (Serial module TX) |
| Ground reference | **GND** | **GND** |
| Power | — NOT CONNECTED — | — NOT CONNECTED — |

This link runs the Meshtastic **Serial module in PROTO mode** on free GPIO pins — see [Meshtastic Setup](meshtastic-setup.md) for the required config (enable, mode PROTO, RX 7, TX 6, 115200, override-console OFF).

> **Why GPIO7/6 and NOT the pads labeled "RX"/"TX" (GPIO44/43)?**
> GPIO43/44 are UART0 — but on the Heltec V3 the **CP2102 USB-UART bridge is wired to those
> exact pins**. The CP2102 is powered from USB; with the Heltec on **battery** (USB unplugged),
> the unpowered bridge **clamps GPIO43/44 to ground** and the Flipper can no longer drive them.
> The old "PhoneAPI on 43/44" wiring therefore only worked while the Heltec was plugged into
> USB power — useless for a deployed, battery-powered backpack.
> Free pins **GPIO7 (RX) and GPIO6 (TX)** have no CP2102 on them, so the Serial module's PROTO
> stream reaches the Flipper on pure battery. Confirmed working on battery 2026-07-01.

---

## Wiring Diagram (ASCII)

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│       Flipper Zero           │         │   Heltec WiFi LoRa 32 V3     │
│    (via Prototype Board)     │         │     (Meshtastic firmware)     │
│                              │         │                              │
│  U_TX / GPIO 13  ───────────────────►  GPIO7   (Serial module RX)    │
│  U_RX / GPIO 14  ◄───────────────────  GPIO6   (Serial module TX)    │
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

2. **Only TX/RX/GND cross between the devices — never a power pin.** Each may be run from USB during development; that's fine on the GPIO6/7 link (no CP2102 on those pins, so no bus contention with the USB console). Just never wire one board's 5V/3.3V to the other.

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

- **Pin 13 = PA9 = USART1_TX** → connect to Heltec **GPIO7** (Serial module RX)
- **Pin 14 = PA10 = USART1_RX** → connect to Heltec **GPIO6** (Serial module TX)
- **Pin 8 or 18 = GND** → connect to Heltec GND

> The Flipper side is fixed by the STM32 — USART1 is always pins 13/14. Only the **Heltec-side**
> pins moved (off 43/44 onto 6/7); do **not** use the pads labeled "TX"/"RX" on the Heltec.

---

## Confirmed GPIO Pin Status (Heltec V3, Meshtastic 2.7.x)

The GhostMesh UART link uses the Serial module on **GPIO7 (RX) / GPIO6 (TX)**. GPIO43/44 are deliberately avoided — see the CP2102 note in the Connection Table.

| GPIO | Status for GhostMesh | Claimed by |
|------|---------------------|-----------|
| **7** | **USED — Serial module RX** (Flipper TX lands here) | GhostMesh |
| **6** | **USED — Serial module TX** (Flipper RX reads here) | GhostMesh |
| 43 | AVOID — CP2102 clamps it on battery | UART0 TX / CP2102 USB console (debug only) |
| 44 | AVOID — CP2102 clamps it on battery | UART0 RX / CP2102 USB console (debug only) |
| 41 | UNAVAILABLE | I2C bus 2 SDA (`i2cInit: sda=41 scl=42`) |
| 42 | UNAVAILABLE | I2C bus 2 SCL |
| 17 | UNAVAILABLE | I2C bus 1 SDA (OLED) |
| 18 | UNAVAILABLE | I2C bus 1 SCL (OLED) |
| 8–14 | UNAVAILABLE | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 19 | UNAVAILABLE | USB D- (ESP32-S3 native USB) |
| 20 | UNAVAILABLE | USB D+ |
| 1 | UNAVAILABLE | Battery ADC |

> GPIO43/44 still carry the Meshtastic USB console over the CP2102 — fine for flashing/debug
> over USB, just not usable as the Flipper link on battery.

---

## End-to-End Verification

**Confirmed working 2026-07-01**, GhostMesh FAP ↔ Heltec on **battery** (no USB):

- FAP reaches `RDY` and shows the node's battery `%` — the `want_config`/`config_complete` handshake completes over the Serial module PROTO stream on GPIO7/6.
- TX (Flipper OK button → mesh) and RX (incoming mesh text in the status bar) both work.
- The link holds steadily on battery power alone — the deployable configuration.

Diagnostic tests that isolated the earlier failure (kept here as a reference troubleshooting ladder):

- **Flipper loopback** — GPIO → USB-UART Bridge, jumper pin 13 → pin 14, PuTTY echoes typed characters (and stops when the jumper is pulled). Confirms the Flipper's USART1 (pins 13/14) works.
- **Node serial** — `client.meshtastic.org` → Serial over the Heltec's USB (CP2102) connects and reads the node. Confirms the node's PROTO/StreamAPI is healthy.
- **The tell:** the web client would connect over the Heltec's *own USB* but not *through the Flipper* on 43/44 while on battery — the CP2102-clamp signature that drove the move to GPIO6/7.

**To verify mesh delivery**, a second Meshtastic node (any Heltec V3 + battery + 915 MHz antenna + phone) receives GhostMesh's sent message in the Meshtastic app Messages view.

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

### BN-220 GPS (Heltec UART1 — GPIO34 RX / GPIO33 TX)

```
BN-220 TX  (white) ──► Heltec GPIO34   (UART1 RX — Heltec receives GPS NMEA)
BN-220 RX  (green) ──► Heltec GPIO33   (UART1 TX — optional, only to reconfigure GPS)
BN-220 VCC (red)   ──► Heltec 3V3       (always-on rail)
BN-220 GND (black) ──► Heltec GND
```

**Do NOT use GPIO35/36** (the original docs were wrong): on the Heltec V3, GPIO35 is the
onboard LED (won't receive UART) and GPIO36 is Vext (powers the OLED — driving it flickers
the screen). Confirmed working pins are **34 (RX) / 33 (TX)**.

Wire colors above are for this BN-220 batch — Beitian varies, so verify yours (red=VCC and
black=GND are universal; swap white/green if no data appears). For battery savings you can
later gate GPS power via Vext (GPIO36, active LOW) — deliberately, since it also drives the
OLED. Cold-start acquisition: 30–90 seconds (needs sky view). Always-on draws 20–40mA.

### HC-SR04 Ultrasonic (Heltec GPIO38 trig / GPIO47 echo)

```
HC-SR04 Trig ──► Heltec GPIO38   (NOT 21 — that's the OLED reset)
HC-SR04 Echo ──► Heltec GPIO47
HC-SR04 VCC  ──► 5V (from USB pin when USB-powered) or 3.3V (verify your module)
HC-SR04 GND  ──► Heltec GND
```

**Voltage note:** Standard HC-SR04 requires 5V for full 4m range. The Heltec 5V pin is
only live when USB is connected. If operating on battery only, use a 3.3V-tolerant clone
(verify the data sheet for your specific module) or add a small boost converter.
GPIO21 is the OLED reset on the Heltec V3 — do NOT use it for the trigger. GPIO38 is free.

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
| Stuck on `...`, connects only when Heltec is on USB power | Wired to GPIO43/44 — CP2102 clamps them on battery | Move the Heltec-side wires to **GPIO7 (RX) / GPIO6 (TX)** |
| Stuck on `...` on battery even on 6/7 | Serial module off, wrong mode, or wrong pins | Meshtastic → Serial: enabled, mode **PROTO**, RX **7**, TX **6**, 115200, override-console OFF |
| No bytes received by Flipper | TX/RX reversed | Swap the TX and RX wires (Flipper 13→Heltec 7, Flipper 14→Heltec 6) |
| Byte counts increase but all garbage | Baud mismatch | Verify both sides at 115200 |
| No data at all | Missing GND | Confirm GND wire is connected |
| Flipper app shows UART ERROR | UART already acquired | Ensure no other Flipper app is using USART1 |
