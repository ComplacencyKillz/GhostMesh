---
---
# BME280 Bench Test (Phase 7)

Standalone diagnostic to confirm the BME280 + STEMMA QT wiring is good **before**
enabling Environment Telemetry in Meshtastic. Proves the I2C bus, the sensor
address (0x76), and that readings are sane.

> Throwaway firmware. Flash to a spare Heltec V3 if you have one (your second
> mesh node works). If you only have the deploy Heltec, reflash Meshtastic when
> finished.

## Bench wiring (USB-powered — do NOT use the Vext rail for this test)

Power from the always-on **3V3** pin so you don't have to drive GPIO26 in firmware.

| Signal | Heltec V3 header | STEMMA QT hub | BME280 |
|--------|------------------|---------------|--------|
| 3.3 V  | <code>3V3</code>            | hub V (any port) | VIN |
| GND    | <code>GND</code>            | hub G            | GND |
| SDA    | <code>GPIO41</code>         | hub SDA          | SDA |
| SCL    | <code>GPIO42</code>         | hub SCL          | SCL |

<pre><code>
Heltec 3V3 ─┐
Heltec GND ─┤  via Qwiic pigtail  ──► STEMMA QT hub port 1
Heltec G41 ─┤  (4 jumpers → Qwiic)      └─ port 2 ──► BME280 (Qwiic)
Heltec G42 ─┘
</code></pre>

Notes:
- The Heltec V3 has **no onboard Qwiic connector** — you need a **Qwiic/STEMMA QT
  pigtail** (Qwiic plug → 4 male jumpers) to reach the header pins. This is the
  one piece people forget.
- **No external pull-ups needed** — the BME280 breakout and the hub already have
  them.
- The hub is passive; any port works for any device.

## Build, flash, monitor

VS Code: PlatformIO sidebar → **Upload**, then **Monitor**. Or CLI from this folder:

<pre><code>
pio run -t upload          # build + flash
pio device monitor -b 115200   # watch output
</code></pre>

## Pass criteria

1. Scan prints <code>found 0x76   <- BME280 (expected)</code>.
2. Readings stream every 2 s with plausible values:
   - **T** within a few °C of room temperature
   - **RH** roughly 30–60 % indoors (breathe on it — humidity should jump)
   - **P** ~950–1040 hPa (≈1013 at sea level; lower at altitude)

Breathe on the sensor: RH and T should move within a second. That confirms it's
the real device, not a stuck value.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Scan finds nothing | SDA/SCL swapped, no 3V3/GND, bad pigtail | Verify all 4 wires; confirm 3V3 not Vext |
| Found at <code>0x77</code> not <code>0x76</code> | SDO pin floating or tied high | Tie SDO→GND, or set <code>BME280_ADDR = 0x77</code> |
| Found <code>0x76</code> but begin() fails | Marginal connection / counterfeit chip | Reseat Qwiic; shorten cable |
| Readings are <code>nan</code> | Intermittent I2C | Shorten cable; reseat; check solder |
| Nothing on serial at all | Wrong port / CDC setting | Confirm CP2102 port; <code>ARDUINO_USB_CDC_ON_BOOT=0</code> is set |

## Next step after this passes

Reflash Meshtastic, then enable **Environment Telemetry** in Module Config (no
custom firmware needed — BME280 is built into Meshtastic 2.7.x). Then teach the
FAP to decode the <code>Telemetry</code> packet (Phase 7 roadmap items).
