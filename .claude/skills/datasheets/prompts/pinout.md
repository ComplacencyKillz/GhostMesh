# Datasheet Pinout Extractor Subagent

You are extracting the **pinout** (per-pin description table) from an electronics component datasheet PDF.

## Task

Read <code>{{PDF_PATH}}</code> (focus pages: <code>{{PAGES}}</code>). Target MPN: **<code>{{MPN}}</code>**.

Produce a single JSON **array** of pin objects matching this schema: <code>{{SCHEMA_PATH}}</code>.

## Field guide (per pin object)

- <code>numbers</code>: array of strings — pin numbers/letters as printed (e.g. <code>["1"]</code> or <code>["A1", "B1"]</code> for pins exposed on multiple package locations). For BGA, use the alphanumeric grid identifier.
- <code>name</code>: pin name as printed (e.g. <code>VIN</code>, <code>OUT</code>, <code>GND</code>, <code>EN</code>, <code>FB</code>, <code>PA0/USART2_CTS</code>).
- <code>type</code>: enum from the schema. Common values: <code>power_in</code>, <code>power_out</code>, <code>input</code>, <code>output</code>, <code>bidirectional</code>, <code>analog_in</code>, <code>analog_out</code>, <code>nc</code> (no-connect), <code>oscillator</code>, <code>reset</code>, <code>boot</code>, <code>debug</code>, <code>clock</code>, <code>data</code>, <code>differential_pair</code>, <code>thermal_pad</code>. Choose the closest semantic match.
- <code>subtype</code>: optional refinement (e.g. <code>switching</code> for a switcher's OUT pin, <code>usb</code>, <code>i2c</code>, <code>spi</code>, <code>pwm</code>).
- <code>description</code>: pin's functional description from the datasheet.
- <code>power_domain</code>: name of the supply rail the pin is referenced to (e.g. <code>VIN</code>, <code>VDD</code>, <code>VDDIO</code>, <code>VBAT</code>). Must be a key in <code>base.recommended_operating</code> when populated. Null for ground or signal pins not domain-tagged.
- <code>alt_functions</code>: array of strings — alternate functions multiplexed onto this pin (MCU pins often have many).
- <code>is_5v_tolerant</code>: bool or null (signal pins on MCUs).
- <code>absolute_max</code>: SpecValue list (per-pin abs max if pin-specific, e.g. CMOS input vs. high-V tolerant input). Null if covered by base block.
- <code>recommended</code>: SpecValue list (per-pin recommended levels).
- <code>drive_strength</code>: SpecValue list for output pins (mA capability).
- <code>notes</code>: free-form caveats.
- <code>evidence</code>: required <code>{page, section, confidence, method}</code>.

## Hard rules

1. **Each pin number gets exactly one entry.** If two pins share a function and number range (e.g. <code>GND</code> on pins 7-12), emit one entry with <code>numbers: ["7","8","9","10","11","12"]</code>.
2. **The pin count must match <code>base.package.pin_count</code>** when known. For thermal pads, emit a separate entry with <code>type: "thermal_pad"</code> if numbered.
3. **Power-domain references** must use the same identifier as the <code>base.recommended_operating</code> keys. If <code>recommended_operating</code> has <code>VIN</code>, the pinout must say <code>power_domain: "VIN"</code> — not <code>"V_IN"</code> or <code>"vin"</code>. The verifier flags mismatches as warnings.
4. **No invention.** If the datasheet doesn't list alt functions, use <code>[]</code>. If 5V-tolerance is unspecified, use <code>null</code>, not <code>false</code>.
5. **Family PDFs**: emit the pinout for the package referenced by <code>{{MPN}}</code>. For LM2596-ADJ, that is the TO-220 NDZ or TO-263 NDH (5-pin). If the variant has multiple package options, pick the one the family-PDF cover lists for the MPN; record the choice in <code>notes</code> of pin 1.

## Output format

Return a JSON array. No prose, no Markdown fences. Output must validate against <code>{{SCHEMA_PATH}}</code>.

Example (LM2596-ADJ, TO-263 NDH):

```json
[
  {"numbers": ["1"], "name": "VIN", "type": "power_in", "subtype": null,
   "description": "Input voltage", "power_domain": "VIN",
   "alt_functions": [], "is_5v_tolerant": null, "absolute_max": null,
   "recommended": null, "drive_strength": null, "notes": null,
   "evidence": {"page": 3, "section": "Pin Configuration and Functions", "confidence": "high", "method": "table"}},
  {"numbers": ["2"], "name": "OUT", "type": "output", "subtype": "switching",
   "description": "Switching output (inductor connects here)", "power_domain": null,
   "alt_functions": [], "is_5v_tolerant": null, "absolute_max": null,
   "recommended": null, "drive_strength": null, "notes": null,
   "evidence": {"page": 3, "section": "Pin Configuration and Functions", "confidence": "high", "method": "table"}}
]
```
