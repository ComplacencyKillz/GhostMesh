# Datasheet Crystal Extractor Subagent

You are extracting the **crystal category extension** (frequency, tolerances, stability, aging, equivalent circuit parameters, package) from an electronics component datasheet PDF.

## Task

Read <code>{{PDF_PATH}}</code> (focus pages: <code>{{PAGES}}</code>). Target MPN: **<code>{{MPN}}</code>**.

Produce a single JSON object matching this schema: <code>{{SCHEMA_PATH}}</code>.

## Field guide

- <code>crystal_type</code>: enum (required). Identifies the resonator technology:
  - <code>"at_cut"</code> — standard AT-cut quartz crystal (most common for MHz-range consumer parts; ceramic-glass sealed SMD parts are typically AT-cut).
  - <code>"ceramic_resonator"</code> — ceramic resonator (lower Q, wider frequency tolerance, lower cost; often labeled "ceramic resonator" or "CERALOCK").
  - <code>"tcxo"</code> — temperature-compensated crystal oscillator (integrated oscillator; outputs a clock signal, not a two-terminal resonator).
  - <code>"vcxo"</code> — voltage-controlled crystal oscillator (integrated oscillator with frequency tuning input).
  - <code>"ocxo"</code> — oven-controlled crystal oscillator (highest stability; integrated oscillator with heater).
  - If the datasheet says "ceramic glass sealed SMD crystal" without specifying cut, use <code>"at_cut"</code> (the dominant cut for MHz-range crystals).

- <code>frequency</code>: SpecValue list (unit: <code>"Hz"</code>). Nominal resonant frequency. **Convert from MHz/kHz to Hz** before storing. 12 MHz → <code>12000000</code>. 32.768 kHz → <code>32768</code>. Found on cover page or frequency/ordering table.

- <code>frequency_tolerance</code>: SpecValue list or null (unit: <code>"ppm"</code>). Initial frequency accuracy at +25°C. Symmetric tolerance stored as min/max (e.g. ±20 ppm → <code>min=-20, max=20</code>). Condition should note reference temperature (e.g. <code>"@ +25°C"</code>).

- <code>frequency_stability</code>: SpecValue list or null (unit: <code>"ppm"</code>). Frequency deviation over the operating temperature range, referenced to +25°C. Stored as min/max symmetric envelope (e.g. ±30 ppm → <code>min=-30, max=30</code>). Condition should state the temperature range (e.g. <code>"-40°C to +85°C, referenced to +25°C"</code>).

- <code>aging</code>: SpecValue list or null (unit: <code>"ppm"</code>). Long-term frequency drift. Stored as min/max symmetric envelope. Condition must note the time window (e.g. <code>"first year"</code> or <code>"per year"</code>). Typical value: ±3 to ±5 ppm for the first year. Do NOT add units suffix <code>/year</code> — use plain <code>"ppm"</code> unit with the time window in the condition.

- <code>load_capacitance</code>: SpecValue list or null (unit: <code>"F"</code>). **Convert pF to Farads.** 10 pF → <code>1e-11</code>. 12 pF → <code>1.2e-11</code>. 18 pF → <code>1.8e-11</code>. This is the capacitive load the oscillator circuit must present for the crystal to hit nominal frequency.

- <code>motional_capacitance</code>: SpecValue list or null (unit: <code>"F"</code>). C1 (series capacitance) in the Butterworth–Van Dyke equivalent circuit. Typically 1–3 femtofarads for AT-cut crystals (e.g. 2 fF → <code>2e-15</code>). Only AT-cut quartz datasheets routinely publish this. Set <code>null</code> when not on the datasheet — this is expected for ceramic resonators, TCXO, VCXO, OCXO.

- <code>motional_inductance</code>: SpecValue list or null (unit: <code>"H"</code>). L1 (series inductance) in the Butterworth–Van Dyke equivalent circuit. Typically millihenry to henry range for AT-cut crystals. Set <code>null</code> when not on the datasheet.

- <code>esr_max</code>: SpecValue list or null (unit: <code>"Ω"</code>). Equivalent series resistance maximum. This is a maximum spec — use the <code>max</code> field. Condition may specify frequency and load capacitance. Typical values: 20–150 Ω for standard crystals; higher for small packages or high-frequency overtone modes.

- <code>drive_level_max</code>: SpecValue list or null (unit: <code>"W"</code>). Maximum crystal excitation power. **Convert µW to Watts.** 100 µW → <code>1e-4</code>. 500 µW → <code>5e-4</code>. This is a maximum spec — use the <code>max</code> field. Exceeding drive level causes aging acceleration.

- <code>operating_temp_range</code>: SpecValue list or null (unit: <code>"°C"</code>). Operating temperature range as min/max. Commercial: 0/+70. Industrial: -40/+85. Automotive: -40/+125.

- <code>mode</code>: enum or null. Resonance mode of operation:
  - <code>"fundamental"</code> — first overtone (most consumer crystals ≤30 MHz)
  - <code>"overtone_3rd"</code> — third overtone (typically 30–100 MHz)
  - <code>"overtone_5th"</code> — fifth overtone (higher frequencies)
  - <code>null</code> for ceramic resonators, TCXO, VCXO, OCXO (the term is not meaningful for these)

- <code>package</code>: object or null. Physical package characteristics. <code>body_mm</code> uses nested <code>{length, width, height}</code> keys (no <code>_mm</code> suffix on inner fields):
  - <code>code</code>: string or null. Datasheet package code (e.g. <code>"SMD-3225"</code>, <code>"SMD-2016"</code>, <code>"HC-49/US"</code>, <code>"SMD-5032"</code>). SMD-3225 = 3.2×2.5 mm plan view.
  - <code>body_mm</code>: nested object with <code>length</code>, <code>width</code>, <code>height</code> (all numbers, in millimeters). Null when dimensions not published.
  - <code>evidence</code>: evidence block (required).

## Hard rules

1. **Canonical SI units. No exceptions.** Frequency in **Hz** (NOT MHz/kHz). Capacitance in **F** (NOT pF). Resistance in **Ω**. Power in **W** (NOT µW). Aging in **ppm** (NOT ppm/year — put the time window in the condition). Tolerance/stability in **ppm**.
2. **Every SpecValue requires <code>evidence</code>** with <code>page</code> (1-based integer), <code>section</code> (string or null), <code>confidence</code> (<code>"high"</code>, <code>"medium"</code>, or <code>"low"</code>), <code>method</code> (one of <code>table</code>, <code>prose</code>, <code>curve</code>, <code>calculated</code>, <code>derived</code>).
3. **<code>motional_capacitance</code> and <code>motional_inductance</code> are expected null** for ceramic resonators, TCXO, VCXO, OCXO. Only AT-cut quartz datasheets routinely publish these. Do not guess.
4. **<code>mode</code> is null for ceramic resonators, TCXO, VCXO, OCXO.** Use <code>"fundamental"</code> for standard AT-cut crystals labeled as fundamental-mode. Use <code>"overtone_3rd"</code> or <code>"overtone_5th"</code> only when the datasheet explicitly states overtone operation.
5. **OMIT fields you cannot find** (leave as null). A null <code>motional_capacitance</code> is correct when the datasheet only provides ESR (R1) and shunt capacitance (C0).
6. **<code>body_mm</code> uses bare keys.** <code>length</code>, <code>width</code>, <code>height</code> — NOT <code>length_mm</code>, <code>width_mm</code>, <code>height_mm</code>.

## Output format

Return only the JSON object. No prose, no fences. Output must validate against <code>{{SCHEMA_PATH}}</code>.

Example (ABM8G-106-12.000MHZ-T — Abracon 12 MHz AT-cut fundamental SMD crystal, 3.2×2.5×1.0 mm):

```json
{
  "crystal_type": "at_cut",
  "frequency": [
    {"min": null, "typ": 12000000.0, "max": null, "unit": "Hz",
     "condition": "Frequency Range, fundamental-mode operation",
     "notes": "Reported as 12.000 MHz; converted from MHz to Hz",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "frequency_tolerance": [
    {"min": -20.0, "typ": null, "max": 20.0, "unit": "ppm",
     "condition": "Frequency Tolerance @ +25°C",
     "notes": "Reported as ±20 ppm",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "frequency_stability": [
    {"min": -30.0, "typ": null, "max": 30.0, "unit": "ppm",
     "condition": "Frequency Stability over -40°C to +85°C, ref +25°C",
     "notes": "Reported as ±30 ppm",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "aging": [
    {"min": -3.0, "typ": null, "max": 3.0, "unit": "ppm",
     "condition": "Aging @ 25°C ±3°C, first year",
     "notes": "±3 ppm/year first-year envelope",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "load_capacitance": [
    {"min": null, "typ": 1e-11, "max": null, "unit": "F",
     "condition": "Load capacitance (CL), typical",
     "notes": "Reported as 10 pF; converted from pF to F (10e-12 = 1e-11)",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "motional_capacitance": null,
  "motional_inductance": null,
  "esr_max": [
    {"min": null, "typ": null, "max": 120.0, "unit": "Ω",
     "condition": "Equivalent series resistance (R1), maximum",
     "notes": null,
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "drive_level_max": [
    {"min": null, "typ": null, "max": 1e-4, "unit": "W",
     "condition": "Drive Level, maximum",
     "notes": "Reported as 100 µW; converted to W (100e-6 = 1e-4)",
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "operating_temp_range": [
    {"min": -40.0, "typ": null, "max": 85.0, "unit": "°C",
     "condition": "Operating Temperature Range",
     "notes": null,
     "evidence": {"page": 1, "section": "Key Electrical Specifications", "confidence": "high", "method": "table"}}
  ],
  "mode": "fundamental",
  "package": {
    "code": "SMD-3225",
    "body_mm": {"length": 3.2, "width": 2.5, "height": 1.0},
    "evidence": {"page": 1, "section": "Package Dimensions", "confidence": "high", "method": "table"}
  }
}
```
