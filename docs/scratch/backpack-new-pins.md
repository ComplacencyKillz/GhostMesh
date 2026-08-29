---
---
New backpack peripheral pins (Heltec V3 / HTIT-WB32LAF)
Verified against the actual board header photo (heltec_front_back/).

Buzzer          -> GPIO39   (GPIO drives a PN2222 transistor base)
Vibration motor -> GPIO40   (GPIO drives a MOSFET gate)
RGB LED         -> GPIO26   (addressable SK6812/WS2812 data; power 3.3V + GND)
Wipe button     -> GPIO37   (input; must be armed + double-press to fire)

IMPORTANT: GPIO 15/16 are NOT broken out on the V3 header (my earlier note was wrong).

Header-available GPIOs (from the board silkscreen):
  top:    7 6 5 4 3 2 1 38 39 40 41 42 45 46 37   (+ 3V3 3V3 GND)
  bottom: 19 20 21 26 48 47 33 34 35 36 0          (+ RST TX RX Ve Ve 5V GND)

Only free + safe (non-strapping) pins were 26 / 37 / 39 / 40 -> now ALL used, zero spare.
Avoid 0/3/45/46 (strapping). 8-14/17-18/27-32 are not broken out (LoRa/OLED/flash).
