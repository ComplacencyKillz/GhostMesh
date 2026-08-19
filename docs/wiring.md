# Wiring Guide

Two independent, battery-powered devices:

- **Flipper Zero** — operator terminal, via the PINGEQUA ProtoBoard that seats on its GPIO header.
- **Heltec WiFi LoRa 32 V3** — the "backpack": Meshtastic radio plus all sensors.

Only **3 wires** cross between the two (TX, RX, GND). **No power is shared** — each device runs from its own battery.

**Status legend:** ✅ wired & working · 🚧 wired, bring-up in progress · ⬜ planned, not yet connected

---

## Current Wiring

Everything in this section is physically connected today.

### Flipper ↔ Heltec — UART link ✅

| Signal | Flipper (ProtoBoard) | Heltec |
|--------|----------------------|--------|
| TX (Flipper → Heltec) | U_TX — pin 13 | GPIO7 |
| RX (Heltec → Flipper) | U_RX — pin 14 | GPIO6 |
| Ground | GND — pin 11 | GND |

Runs the Meshtastic **Serial module in PROTO mode**, 115200 8N1. Uses GPIO6/7 rather than the pads labelled TX/RX (GPIO43/44) because the Heltec's CP2102 USB bridge clamps 43/44 on battery. Module config: [Meshtastic Setup](meshtastic-setup.md).

### Heltec ↔ BN-220 GPS ✅

| Heltec | BN-220 (wire) |
|--------|---------------|
| GPIO34 (UART1 RX) | TX — white |
| GPIO33 (UART1 TX) | RX — green |
| 3V3 | VCC — red |
| GND | GND — black |

### Heltec ↔ I2C Hub (STEMMA QT 5-port) ✅

| Heltec | Hub (wire) |
|--------|------------|
| GPIO41 | SDA — yellow |
| GPIO42 | SCL — orange |
| 3V3 | VIN — brown |
| GND | GND — red |

### I2C Hub ↔ BME280 ✅

| BME280 | Hub (wire) |
|--------|------------|
| VCC | VIN — brown |
| GND | GND — red |
| SCL | SCL — orange |
| SDA | SDA — yellow |

I2C addresses: **BME280 = 0x76** (bus 2). The OLED (0x3C) is on bus 1 (GPIO17/18), hardwired — leave it alone.

### I2C Hub ↔ MAX17048 Fuel Gauge 🚧

Plugged into the STEMMA QT hub via Qwiic (powered, address **0x36** on bus 2). **Not functional / untested** — the MAX17048's battery port is a 2.0 mm JST-PH but the Heltec cell uses a 1.25 mm JST, so the pack it's meant to measure can't be connected yet. Needs a connector adapter before it can read state-of-charge.

### Heltec ↔ SW-520D Tilt Switch ✅

External pull-down, per the board schematic (`kicad/`):

| Node | Connects to |
|------|-------------|
| Tilt switch leg 1 | 3.3V |
| Tilt switch leg 2 | GPIO2 (junction) |
| 10kΩ (R3) | GPIO2 → GND |

Non-polarized switch. Idle (open) = LOW via the 10kΩ pull-down; closed = HIGH. **Working** — broadcasts `TAMPER` over LoRa via the custom `heltec-firmware/TiltModule`, gated by the arm switch. **Disable the built-in Detection Sensor** in the Meshtastic app (Module Config → Detection Sensor → OFF) — TiltModule owns GPIO2. Requires a private channel; see [meshtastic-setup.md](meshtastic-setup.md).

### Heltec ↔ Slide Switch (Arm/Disarm) ✅

SPDT slide switch on GPIO4, per the schematic:

| Node | Connects to |
|------|-------------|
| Common | GPIO4 |
| One throw | 3.3V (armed) |
| Other throw | GND (disarmed) |

Runs on `heltec-firmware/ArmingModule` → broadcasts `ARMED` / `DISARMED` and sets the shared `ghostmesh_armed` state that gates the tilt/light/proximity modules (they only alert when armed). The switch is read as a **toggle** — any flip inverts the state, the position isn't tied to a state — so it never disagrees with an IR/mesh arm/disarm (last action wins). Boot state is DISARMED regardless of switch position, so wiring polarity doesn't matter.

### Heltec ↔ Photoresistor (Light Tamper) ✅

Voltage divider on GPIO5 (ADC1), per the board schematic (`kicad/`):

| Node | Connects to |
|------|-------------|
| 10kΩ (R2) | 3.3V → GPIO5 (junction) |
| Photoresistor (R1) | GPIO5 (junction) → GND |
| GPIO5 | junction |

With the 10kΩ on the 3.3V side, **bright light lowers the ADC reading** — so the custom `LightTamperModule` broadcasts `TAMPER_LIGHT` when the reading drops below a threshold. **Working** — the default threshold (2000) triggers cleanly. Runs on `heltec-firmware/LightTamperModule` (see [developer-guide.md](developer-guide.md)).

### Heltec ↔ HC-SR04 Ultrasonic (Proximity) 🚧

| HC-SR04 | Heltec |
|---------|--------|
| VCC | **5V** (not 3.3V — see note) |
| Trig | GPIO38 |
| Echo | 1kΩ → GPIO47 (junction), then GPIO47 → 2kΩ → GND |
| GND | GND |

The plain blue HC-SR04 **does not work at 3.3V** (reads 0 cm). It needs **5V**, and its 5V Echo must be divided to 3.3V before GPIO47 (1kΩ/2kΩ). **Working on the bench (USB 5V)** via `heltec-firmware/ProximityModule` → broadcasts `PERSON_DETECTED`. The battery backpack has no 5V, so deployment uses a **3.3V RCWL-1601 / JSN-SR04T** (drop-in, no code change).

### Heltec ↔ IR Receiver (Arm/Disarm) ✅

VS1838B / KY-022 on GPIO48 (this module's pins are labelled by wire colour):

| IR module | Heltec |
|-----------|--------|
| Y (signal) | GPIO48 |
| R (VCC) | 3.3V |
| G (GND) | GND |

Runs on `heltec-firmware/IRModule` → decodes NEC codes and arms/disarms (sets `ghostmesh_armed`, broadcasts `ARMED`/`DISARMED`), alongside the slide switch (last action wins). Works with any NEC remote, or the Flipper as a dedicated remote via `flipper-app/GhostMeshBackpack.ir`. Button codes live in `IRModule.cpp` (`IR_ARM_CODE` / `IR_DISARM_CODE`).

---

## Planned Wiring

Reserved assignments for components not yet connected. Do not treat these as built.

### Heltec backpack — outputs & controls ⬜

Driven by `heltec-firmware/CommandModule` over the mesh / IR — **no Flipper hardware**. Pins verified
against the board header photo. Buzzer + vibration use a **PN2222** low-side driver on the bench; the
EE PCB swaps the motor driver for an **AO3400** MOSFET (identical firmware — a low-side switch is
`HIGH`=on either way). The arming slide switch is on Heltec GPIO4 (`ArmingModule`).

| Control | Heltec GPIO | Circuit |
|---------|-------------|---------|
| Passive buzzer | GPIO39 | GPIO39 → 1kΩ → PN2222 base; collector → buzzer(−); emitter → GND; buzzer(+) → 3V3. **1N4007 across the buzzer, stripe → 3V3** (it's a magnetic coil, ~15Ω). Firmware drives a PWM **tone**, not DC. |
| Vibration motor | GPIO40 | GPIO40 → 1kΩ → PN2222 base; collector → motor; other motor lead → 3V3; emitter → GND. **1N4007 flyback across the motor, stripe → 3V3, is mandatory.** Plain on/off. |
| RGB status LED | GPIO26 | SK6812 addressable — DIN ← GPIO26; VDD → 3V3; VSS → GND (planned). |
| Wipe button | GPIO37 | Tact switch: one side → GPIO37, other → GND; firmware uses INPUT_PULLUP (pressed = LOW). Armed + double-press to fire. |

---

## Reference

### Flipper ProtoBoard pinout (PINGEQUA v3.1)

```
Pin  1     2     3     4     5     6     7     8
     5V    PA7   PA6   PA4   SWO   PB2   PC3   GND
Pin  9     10    11    12    13    14    15    16    17    18
     3.3V  SWCLK GND   SWDIO U_TX  U_RX  PC1   PC0   PB14  GND
```

Numbers and labels match the Flipper case. The external UART is fixed by the STM32 at pins **13 (U_TX)** / **14 (U_RX)**. GND is available on pins 8, 11, or 18.

### Heltec V3 GPIO allocation

| GPIO | Use |
|------|-----|
| 6 / 7 | Serial-module TX / RX ↔ Flipper ✅ |
| 33 / 34 | GPS TX / RX (UART1) ✅ |
| 41 / 42 | I2C bus 2 — hub (BME280 0x76 ✅; MAX17048 0x36 on-bus, untested 🚧) |
| 17 / 18 | I2C bus 1 — OLED (0x3C), hardwired |
| 2 | Tilt switch ✅ |
| 5 | Photoresistor — light tamper (LightTamperModule) ✅ |
| 38 / 47 | HC-SR04 proximity — trig / echo (ProximityModule) 🚧 |
| 4 | Slide switch — arm/disarm (ArmingModule) ✅ |
| 48 | IR receiver — arm/disarm (IRModule) ✅ |
| 8–14 | SX1262 LoRa SPI + IRQ/RST/BUSY |
| 19 / 20 | Native USB D− / D+ |
| 1 | Battery ADC |
| 43 / 44 | UART0 / CP2102 USB console — **avoid** (clamps on battery; fine for USB flashing/debug) |
| 21 | OLED reset — do not reuse |
| 35 | Onboard LED — not a usable UART pin |
| 36 | Vext — powers OLED + external 3V3 rail (active-LOW) |

### Electrical rules

- **Never** connect 5V or 3.3V between the Flipper and Heltec — separate batteries. Only TX/RX/GND cross.
- A shared **GND is required** for UART.
- Both sides are 3.3V logic — no level shifter needed.
- Baud is **115200 8N1** (Meshtastic Serial default). If changed, update both `GHOSTMESH_UART_BAUD` in `uart_helper.h` and the module config.

---

## Troubleshooting — Flipper ↔ Heltec link

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Stuck on `...`, only connects on USB power | Wired to GPIO43/44 (CP2102 clamps on battery) | Move to GPIO7 (RX) / GPIO6 (TX) |
| Stuck on `...` on battery even on 6/7 | Serial module off / wrong mode / wrong pins | Meshtastic → Serial: enabled, PROTO, RX 7, TX 6, 115200, console off |
| No bytes received | TX/RX swapped | Swap: Flipper 13 → Heltec 7, Flipper 14 → Heltec 6 |
| Bytes increase but garbage | Baud mismatch | Both sides at 115200 |
| No data at all | Missing GND | Confirm the GND wire |
