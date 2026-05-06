# Hardware Reference

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
| U_TX | 13 | USART1 TX → Heltec GPIO44 |
| U_RX | 14 | USART1 RX ← Heltec GPIO43 |
| GND | 8 or 18 | Ground reference (shared with Heltec) |
| PB2 | 15 | Slide switch (arming gate) |
| PB3 | 16 | SW-520D tilt switch (tamper detection) |
| PA7 | 5 | Active buzzer via PN2222 transistor |
| PA6 | 6 | Vibration motor via AO3400 MOSFET + 1N4007 flyback diode |

---

## Heltec WiFi LoRa 32 V3 (MakerHawk compatible)

- **MCU:** ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **Radio:** SX1262 LoRa transceiver
- **Frequency:** 915 MHz (US/AU band)
- **Antenna:** 915 MHz whip, SMA or IPEX connector
- **USB:** USB-C + CP2102 UART bridge — powered from Heltec battery always-on
- **Battery:** JST-PH connector for LiPo
- **Display:** 0.96" OLED (128×64), managed by Meshtastic firmware

### Power Architecture

```
[Flipper battery]  ──►  [Flipper Zero]       (independent — do not share)
[Heltec battery]   ──►  [Heltec ESP32-S3]    (independent — do not share)
                              │
                         [GPIO26 Vext]  ──► [External sensor 3.3V rail]
                                            (software power gate)
```

**Never connect Flipper 3.3V or 5V to Heltec Vcc.** The Flipper's 3.3V regulator cannot
source the 200–500mA an ESP32-S3 draws under load. Only TX, RX, and GND are wired between the devices.

### Heltec GPIO Allocation

| GPIO | Status | Assigned to |
|------|--------|-------------|
| 1 | ❌ Battery ADC | Do not use for other analog inputs |
| 5 | ✅ Free (ADC1_CH4) | Photoresistor (light tamper sensor) |
| 7 | ✅ Confirmed free | Reserve / available |
| 8–14 | ❌ SX1262 LoRa SPI | NSS, SCK, MOSI, MISO, RST, BUSY, DIO1 |
| 17 | ❌ I2C bus 1 SDA | OLED display (hardwired) |
| 18 | ❌ I2C bus 1 SCL | OLED display (hardwired) |
| 19 | ❌ USB D- | ESP32-S3 native USB |
| 20 | ❌ USB D+ | ESP32-S3 native USB |
| 21 | ✅ Likely free | HC-SR04 Trigger (verify against board silkscreen) |
| 26 | ⚠️ Vext control | External 3.3V enable — drive HIGH to power sensors |
| 35 | ✅ Free | GPS UART1 RX (BN-220 TX) |
| 36 | ✅ Free | GPS UART1 TX (BN-220 RX — optional for GPS config) |
| 41 | ❌ I2C bus 2 SDA | Sensor I2C bus (BME280, MAX17048 via Qwiic hub) |
| 42 | ❌ I2C bus 2 SCL | Sensor I2C bus |
| 43 | ❌ UART0 TX | Meshtastic PhoneAPI → Flipper |
| 44 | ❌ UART0 RX | Meshtastic PhoneAPI ← Flipper |
| 47 | ✅ Free | HC-SR04 Echo |
| 48 | ✅ Free | IR receiver module |

---

## Sensor Bill of Materials

### Already Ordered / Confirmed

| Component | Interface | I2C Addr | Heltec GPIO | Phase |
|-----------|-----------|----------|-------------|-------|
| BME280 (temp/humidity/pressure) | I2C via Qwiic hub | 0x76 | Bus 2 (41/42) | 7 |
| MAX17048 (LiPo fuel gauge) | I2C via Qwiic hub | 0x36 | Bus 2 (41/42) | 9 |
| BN-220 GPS module | UART1, 9600 baud | — | GPIO35 (RX), GPIO36 (TX) | 8 |
| STEMMA QT 5-port passive hub | — | — | GPIO41/42 | 7 |
| HC-SR04 ultrasonic sensor | Digital GPIO | — | GPIO21 (trig), GPIO47 (echo) | 11 |
| SW-520D tilt switch | Digital GPIO | — | Flipper pin 16 | 10 |
| Active buzzer | Digital GPIO | — | Flipper pin 5 (via PN2222) | 10 |
| Coin vibration motor (3V) | Digital GPIO PWM | — | Flipper pin 6 (via AO3400) | 10 |
| Slide switch (SPDT) | Digital GPIO | — | Flipper pin 15 | 10 |
| AO3400 N-MOSFET (SOT-23) | — | — | Gate from Flipper pin 6 | 10 |
| 1N4007 diode | — | — | Across vibration motor | 10 |
| PN2222 NPN transistor | — | — | Base from Flipper pin 5 | 10 |

### From Elegoo Super Starter Kit (relevant components)

| Component | Use in GhostMesh | Phase |
|-----------|-----------------|-------|
| Photoresistor | Case-open / light tamper on Heltec GPIO5 | 10 |
| IR receiver module | Covert remote arm/disarm on Heltec GPIO48 | 10 |
| 1N4007 diode rectifier (2pcs) | Flyback protection for vibration motor | 10 |
| PN2222 NPN transistor (2pcs) | Buzzer driver | 10 |
| Active + passive buzzer (2pcs) | Audible alerts | 10 |

**Not used:** DHT11 (redundant — BME280 is strictly better), LCD 1602 (both devices have displays),
stepper motor, servo, joystick, potentiometer, UNO R3, 7-segment displays.

### Connectivity Hardware

| Item | Use |
|------|-----|
| JST PH 2.0mm connector kit | Battery connections, sensor power |
| STEMMA QT / Qwiic cables (10/20/30/50cm) | I2C sensor chain |
| PINGEQUA ProtoBoard (Flipper Zero) | Clean GPIO breakout for Flipper-side sensors |
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
UART0 (GPIO43 TX / GPIO44 RX):
  └── Meshtastic PhoneAPI
        ├── Flipper PROTO frames (ToRadio / FromRadio protobuf)
        └── Heltec ASCII sentinels (TAMPER_LIGHT, PROX, JAMMER, IR_ARM, IR_SEND_n)

UART1 (GPIO35 RX / GPIO36 TX):
  └── BN-220 GPS module (NMEA-0183, 9600 baud)
        └── Meshtastic reads and parses for position beaconing
```

---

## Communication Chain

```
[Flipper Zero]
      │
      │ UART 115200 (PROTO + ASCII sentinels)
      │
[Heltec ESP32-S3]
      ├── [SX1262 LoRa] ── 915 MHz mesh ── [Other Meshtastic nodes]
      ├── [OLED] — Meshtastic display
      ├── [BME280] — env telemetry via Meshtastic
      ├── [MAX17048] — battery SOC via custom module
      ├── [BN-220 GPS] — position via Meshtastic
      ├── [HC-SR04] — proximity → PROX sentinel to Flipper
      ├── [Photoresistor] — tamper → TAMPER_LIGHT sentinel to Flipper
      └── [IR receiver] — covert remote → IR_ARM/IR_DISARM to Flipper
```

---

## Confirmed Working State

- GhostMesh FAP shows `RDY` after a few seconds startup handshake
- OK button sends selected canned message over LoRa mesh
- Incoming text messages from other nodes appear in GhostMesh status bar
- Long-press Down opens RX history with RSSI/SNR per message
- CSV log written to SD on every received message
- Two-node end-to-end confirmed: Flipper → f69c → LoRa → 2f74 (TX and RX both working)
