# Hardware Reference

> **Board schematic (source of truth):** the KiCad design lives in `kicad/` —
> `kicad/FlipperZeroModule/` (schematic + PCB) and `kicad/HeltecModule/` (Heltec symbol/footprint).
> Where this document and the schematic disagree, the schematic wins.

## Component Spec

Every part in the build, with the maker and part marking to chase down a datasheet. Search the
**part** column plus "datasheet" to find each one.

| Component | Part / marking | Maker | Role in GhostMesh | Interface |
|-----------|----------------|-------|-------------------|-----------|
| Flipper Zero | STM32WB55 | Flipper Devices | Operator terminal (runs the FAP) | — |
| Heltec WiFi LoRa 32 V3 | HTIT-WB32LAF | Heltec | Backpack MCU + radio board | — |
| MCU (on Heltec) | ESP32-S3 | Espressif | Backpack processor | — |
| LoRa radio (on Heltec) | SX1262 | Semtech | 915 MHz LoRa transceiver | SPI (internal) |
| Environment sensor | BME280 (GY-BME280) | Bosch Sensortec | Temp / humidity / pressure | I2C 0x76 |
| GPS module | BN-220 | u-blox-based | Position / time | UART 9600 (NMEA) |
| Fuel gauge | MAX17048 | Analog Devices (Maxim) | LiPo state-of-charge | I2C 0x36 |
| Tilt switch | SW-520D | generic | Tamper — node moved | GPIO |
| Light sensor | GL5528 photoresistor (LDR) | generic | Tamper — case opened | ADC |
| Ultrasonic ranger | HC-SR04 (deploy: RCWL-1601) | generic | Proximity — approach | GPIO (5 V) |
| IR receiver | VS1838B | generic | NEC IR remote control | GPIO (38 kHz demod) |
| RGB indicator | SK6812 | Adafruit-compatible | Status LED (planned) | 1-wire addressable |
| Buzzer | passive magnetic buzzer | generic | Audible indicator (tones) | GPIO PWM via driver |
| Haptic | 3 V coin/cyl vibration motor | generic | Vibration indicator | GPIO via driver |
| Driver (bench) | PN2222A (TO-92) | generic NPN BJT | Low-side switch for buzzer/motor | — |
| Driver (PCB) | AO3400 (SOT-23) | Alpha & Omega | Low-side switch for the motor | — |
| Flyback diode | 1N4007 | generic | Coil flyback protection | — |
| I2C hub | STEMMA QT 5-port | Adafruit | Passive I2C fan-out | I2C passthrough |
| Wipe button | 6 mm tact switch | generic | Physical destruct trigger | GPIO (INPUT_PULLUP) |
| Arming switch | SPDT slide switch | generic | Toggle the arm state | GPIO |
| Antenna | 915 MHz whip (SMA/IPEX) | generic | LoRa antenna | — |
| Battery | LiPo, JST-PH 2.0 | generic | Backpack power (independent) | — |

The per-device wiring, GPIO map, and driver circuits are below and in [wiring.md](wiring.md).

## Flipper Zero

- **MCU:** STM32WB55 (ARM Cortex-M4 + M0+)
- **Display:** 128×64 monochrome LCD
- **GPIO header:** Available via the official prototype board
- **UART:** USART1 on header pins 13 (TX) / 14 (RX)
- **Firmware:** Stock Flipper firmware or compatible fork (tested with official firmware)

### Prototype Board

The official Flipper Zero prototype board (or PINGEQUA ProtoBoard) breaks out the GPIO
header into a breadboard-friendly form. Required for sensor connections.

| Flipper Label | Header Pin | Function |
|---------------|-----------|----------|
| U_TX | 13 | USART1 TX → Heltec GPIO7 (Serial module RX) |
| U_RX | 14 | USART1 RX ← Heltec GPIO6 (Serial module TX) |
| GND | 8 or 18 | Ground reference (shared with Heltec) |

Only these three wires run between the Flipper and the backpack. The Flipper hosts **no** control
hardware — passive buzzer, vibration motor, RGB LED, arming slide, and wipe button all live on the
**Heltec backpack** (see the Heltec GPIO allocation below), triggered via the FAP, mesh, or IR.

---

## Heltec WiFi LoRa 32 V3 (MakerHawk compatible)

- **MCU:** ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **Radio:** SX1262 LoRa transceiver
- **Frequency:** 915 MHz (US/AU band)
- **Antenna:** 915 MHz whip, SMA or IPEX connector
- **USB:** USB-C + CP2102 UART bridge on UART0/GPIO43-44 — **powered from USB (5V VBUS), NOT the battery.** When the Heltec runs on battery (USB out) the CP2102 is unpowered and clamps GPIO43/44, which is why the Flipper link uses GPIO6/7 instead (see [wiring.md](wiring.md))
- **Battery:** JST-PH connector for LiPo
- **Display:** 0.96" OLED (128×64), managed by Meshtastic firmware

### Power Architecture

```
[Flipper battery]  ──►  [Flipper Zero]       (independent — do not share)
[Heltec battery]   ──►  [Heltec ESP32-S3]    (independent — do not share)
                              │
                         [GPIO36 Vext]  ──► [OLED + external 3.3V rail]
                                            (software power gate — drives the OLED too)
```

**Never connect Flipper 3.3V or 5V to Heltec Vcc.** The Flipper's 3.3V regulator cannot
source the 200–500mA an ESP32-S3 draws under load. Only TX, RX, and GND are wired between the devices.

### Heltec GPIO Allocation

The Heltec is the **backpack brain** — it operates fully unattended. All sensors
that need to function without the Flipper present are wired here and handled by
custom Meshtastic modules.

| GPIO | Status | Assigned to |
|------|--------|-------------|
| 1 | ❌ Battery ADC | Do not use for other analog inputs |
| 2 | ✅ Free | SW-520D tilt switch (tamper — backpack moved/disturbed) |
| 4 | ✅ Free | Slide switch (physical arm/disarm when deploying backpack) |
| 5 | ✅ Free (ADC1_CH4) | Photoresistor (light tamper — case opened) |
| 6 | ❌ Serial module TX | **GhostMesh Flipper link** — Heltec → Flipper (to Flipper pin 14 RX) |
| 7 | ❌ Serial module RX | **GhostMesh Flipper link** — Flipper → Heltec (from Flipper pin 13 TX) |
| 8–14 | ❌ SX1262 LoRa SPI | NSS, SCK, MOSI, MISO, RST, BUSY, DIO1 |
| 17 | ❌ I2C bus 1 SDA | OLED display (hardwired) |
| 18 | ❌ I2C bus 1 SCL | OLED display (hardwired) |
| 19 | ❌ USB D- | ESP32-S3 native USB |
| 20 | ❌ USB D+ | ESP32-S3 native USB |
| 21 | ❌ OLED reset | Hardwired OLED reset — NOT free (HC-SR04 trigger uses GPIO38 instead) |
| 26 | 🚧 RGB status LED | External SK6812 data line (`/led`, planned) — NOT Vext (that's GPIO36) |
| 33 | ✅ Free — confirmed | GPS UART1 TX (Heltec → BN-220 RX) |
| 34 | ✅ Free — confirmed | GPS UART1 RX (BN-220 TX → Heltec) |
| 35 | ❌ Onboard LED | White user LED (does NOT work as a UART RX); `CommandModule` `/led` placeholder until GPIO26 RGB is wired |
| 36 | ❌ Vext control | Powers OLED + external 3.3V rail (Meshtastic `VEXT_ENABLE`, active LOW) |
| 37 | 🚧 Wipe button | Tact switch, INPUT_PULLUP → `CommandModule` factory reset (armed + double-press) |
| 39 | 🚧 Buzzer | Passive buzzer via PN2222 low-side driver — PWM tone (`CommandModule` `/buzz`) |
| 40 | 🚧 Vibration | Motor via PN2222 + 1N4007 flyback (`CommandModule` `/vibrate`); EE PCB uses AO3400 |
| 41 | ❌ I2C bus 2 SDA | Sensor I2C bus (BME280, MAX17048 via Qwiic hub) |
| 42 | ❌ I2C bus 2 SCL | Sensor I2C bus |
| 43 | ⚠️ UART0 TX / CP2102 | USB console (flash/debug). **NOT the Flipper link** — CP2102 clamps it on battery |
| 44 | ⚠️ UART0 RX / CP2102 | USB console (flash/debug). **NOT the Flipper link** — CP2102 clamps it on battery |
| 47 | ✅ Free | HC-SR04 Echo |
| 48 | ✅ Free | IR receiver (NEC decode — remote arm/disarm ~10m) |

---

## Sensor Bill of Materials

### Already Ordered / Confirmed

**Heltec backpack sensors** — operate unattended, handled by custom Meshtastic modules:

| Component | Interface | I2C Addr | Heltec GPIO | Phase |
|-----------|-----------|----------|-------------|-------|
| BME280 (temp/humidity/pressure) | I2C via Qwiic hub | 0x76 | Bus 2 (41/42) | 7 |
| MAX17048 (LiPo fuel gauge) | I2C via Qwiic hub | 0x36 | Bus 2 (41/42) | 9 |
| BN-220 GPS module | UART1, 9600 baud | — | GPIO34 (RX), GPIO33 (TX) | 8 |
| STEMMA QT 5-port passive hub | — | — | GPIO41/42 | 7 |
| HC-SR04 ultrasonic sensor | Digital GPIO | — | GPIO38 (trig — was 21, which is OLED reset), GPIO47 (echo) | 11 |
| SW-520D tilt switch | Digital GPIO | — | GPIO2 | 10 |
| Slide switch — backpack arm/disarm | Digital GPIO | — | GPIO4 | 10 |
| Photoresistor (light tamper) | ADC | — | GPIO5 | 10 |
| IR receiver (remote arm/disarm) | Digital GPIO | — | GPIO48 | 10 |

**Heltec backpack outputs & controls** — operator triggers them over the FAP / mesh / IR; driven by `CommandModule`:

| Component | Interface | Heltec GPIO | Phase |
|-----------|-----------|-------------|-------|
| Passive buzzer | Digital GPIO via PN2222 → PWM tone | GPIO39 | 10 |
| Vibration motor (3V) | Digital GPIO via PN2222 (PCB: AO3400) | GPIO40 | 10 |
| RGB status LED (SK6812) | Addressable data line | GPIO26 | 10 |
| Wipe button (tact switch) | Digital GPIO, INPUT_PULLUP | GPIO37 | 10 |
| PN2222 NPN transistor (x2) | Low-side drivers for buzzer + motor | Base via 1kΩ | 10 |
| 1N4007 diode | Flyback across the motor (and the coil buzzer) | — | 10 |

### From Elegoo Super Starter Kit (relevant components)

| Component | Use in GhostMesh | Where | Phase |
|-----------|-----------------|-------|-------|
| Photoresistor | Light tamper — case opened | Heltec GPIO5 | 10 |
| IR receiver module | Remote arm/disarm | Heltec GPIO48 | 10 |
| 1N4007 diode rectifier (2pcs) | Flyback for the vibration motor + coil buzzer | Heltec backpack | 10 |
| PN2222 NPN transistor (2pcs) | Buzzer (GPIO39) + vibration (GPIO40) low-side drivers | Heltec backpack | 10 |
| Passive buzzer | Tone alerts (distinct tones per event) via `/buzz` | Heltec GPIO39 | 10 |

**Not used:** DHT11 (redundant — BME280 is strictly better), LCD 1602 (both devices have
displays), stepper motor, servo, joystick, potentiometer, UNO R3, 7-segment displays, the
**active buzzer** (the passive one is used instead, so alerts can carry distinct tones).

### Connectivity Hardware

| Item | Use |
|------|-----|
| JST PH 2.0mm connector kit | Battery connections, sensor power |
| STEMMA QT / Qwiic cables (10/20/30/50cm) | I2C sensor chain |
| PINGEQUA ProtoBoard | Backpack substrate — hosts the Heltec + sensors/outputs; plugs onto the Flipper GPIO (TX/RX/GND) |
| M2 / M2.5 nylon standoff kits | Mechanical mounting |
| Dupont wires (from Elegoo kit) | Breadboard prototyping before final assembly |

---

## I2C Bus Architecture

The Heltec V3 has two independent I2C buses. Do not mix them.

```
Bus 1 (GPIO17 SDA / GPIO18 SCL):
  └── OLED display (0x3C) — hardwired to board, no Qwiic connector

Bus 2 (GPIO41 SDA / GPIO42 SCL):
  └── STEMMA QT 5-port passive hub
        ├── BME280 (0x76) — temp/humidity/pressure
        └── MAX17048 (0x36) — LiPo fuel gauge
```

No address conflicts between these three devices. The Qwiic hub is passive (no active
I2C muxing), so all devices share the same bus with distinct addresses.

---

## UART Architecture

```
Serial module — PROTO (GPIO7 RX / GPIO6 TX):   ← GhostMesh Flipper link
  └── Meshtastic StreamAPI over the Serial module (ToRadio / FromRadio protobuf only)
        └── Sensor alerts arrive here too — as FromRadio mesh packets, not a separate protocol

UART0 (GPIO43 TX / GPIO44 RX):
  └── CP2102 USB console — flashing + debug over USB only
        (NOT the Flipper link — unpowered CP2102 clamps these on battery)

UART1 (GPIO34 RX / GPIO33 TX):
  └── BN-220 GPS module (NMEA-0183, 9600 baud)
        └── Meshtastic reads and parses for position beaconing
```

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  BACKPACK (planted, runs unattended)                            │
│                                                                 │
│  [Heltec ESP32-S3 + Meshtastic + GhostMesh modules]             │
│    ├── [SX1262 LoRa] ──── 915 MHz mesh ──── [teammate nodes]   │
│    ├── [BME280]           env telemetry — stock Meshtastic      │
│    ├── [BN-220 GPS]       position — stock Meshtastic           │
│    ├── [SW-520D tilt]     tamper → TAMPER over LoRa (armed)     │
│    ├── [Photoresistor]    case-open → TAMPER_LIGHT (armed)      │
│    ├── [HC-SR04]          proximity → PERSON_DETECTED (armed)   │
│    ├── [IR receiver]      arm / disarm / destruct (line of sight)│
│    ├── [toggle switch]    flip to arm/disarm                    │
│    ├── [buzzer/motor/LED] indicators — driven over mesh or IR   │
│    └── [wipe button]      destruct (armed + double-press)       │
└─────────────────────────────────────────────────────────────────┘
                    │ attached: UART 115200, PROTO frames
                    │ detached: 915 MHz mesh + line-of-sight IR
                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  OPERATOR                                                       │
│                                                                 │
│  [Flipper Zero + GhostMesh FAP]                                 │
│    ├── terminal — mesh messaging, telemetry, RX log             │
│    ├── Control screen → IR arm / disarm / destruct              │
│    └── encrypted config backup → SD                             │
│   (no control hardware on the Flipper — every output lives on   │
│    the backpack, triggered over the mesh or by IR)              │
└─────────────────────────────────────────────────────────────────┘
```

### What Requires Custom Meshtastic Firmware

| Feature | Stock Meshtastic | Custom module needed |
|---------|-----------------|---------------------|
| BME280 env telemetry | ✅ built-in | — |
| BN-220 GPS | ✅ built-in | — |
| Private channels / config | ✅ AdminMessage + config | — |
| Complete-flash destruct | ⚠️ AdminMessage only resets config | ✅ GhostMeshWipe (built) |
| Mesh command CLI (`/cmd @target`) | ❌ | ✅ CommandModule (built) |
| HC-SR04 → LoRa alert | ❌ | ✅ ProximityModule (built) |
| Tilt switch → LoRa alert | built-in exists but isn't arm-gated | ✅ TiltModule (used) |
| Slide switch arm/disarm + gate | ❌ | ✅ ArmingModule (built) |
| Photoresistor → LoRa alert | ❌ | ✅ LightTamperModule (built) |
| IR receiver arm/disarm | ❌ | ✅ IRModule (built) |
| MAX17048 accurate SOC | ❌ | Custom module |
| Jammer detection | ❌ | Custom module |
| UART encryption | ❌ | Full custom firmware layer |

---

## Confirmed Working State

- FAP: menu-hub UI (Messages / RX History / Sensors / Control / Status / Backup); `RDY` after the handshake
- TX/RX text over the mesh; per-message RSSI/SNR; dated CSV logging; marquee display
- Telemetry: BME280 temp/humidity/pressure, BN-220 GPS position, battery % in the title bar
- Backpack firmware: tamper (tilt / light), proximity (bench), arming toggle, buzzer + vibration — all over the private mesh, arm-gated
- IR control: arm / disarm confirmed on hardware; the `ARM → WIPE → CONFIRM` destruct + complete-flash wipe built (spare-board test pending)
- Encrypted config backup written by the FAP (backup → restore round-trip test pending)

Full phase-by-phase status: [roadmap.md](roadmap.md).
