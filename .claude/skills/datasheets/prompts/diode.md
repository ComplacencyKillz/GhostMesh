---
---
# Datasheet Diode Extractor Subagent

You are extracting the **diode category extension** (forward/reverse ratings, recovery, capacitance, package, thermal limits) from an electronics component datasheet PDF.

## Task

Read <code>{{PDF_PATH}}</code> (focus pages: <code>{{PAGES}}</code>). Target MPN: **<code>{{MPN}}</code>**.

Produce a single JSON object matching this schema: <code>{{SCHEMA_PATH}}</code>.

## Field guide

- <code>diode_type</code>: enum. Choose the single closest match: <code>signal</code> (general-purpose small-signal), <code>switching</code> (fast switching, e.g. 1N4148), <code>schottky</code> (metal-semiconductor junction, low Vf), <code>zener</code> (voltage reference/clamp), <code>tvs</code> (transient voltage suppressor), <code>rectifier</code> (power rectifier, slow), <code>bridge</code> (bridge rectifier array), <code>varicap</code> (voltage-variable capacitor / tuning diode). The cover/Features section usually announces the type.
- <code>vf</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Multiple SpecValues when the datasheet gives Vf at different If test currents. Carry each test condition in <code>condition</code> (e.g. <code>"IF=10mA"</code>, <code>"iF=5A, TC=25°C"</code>). Found in Electrical Characteristics table.
- <code>if_max</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>). Emit separate SpecValues for continuous (IF), average rectified (IF(AV)), and repetitive peak (IFRM) variants — disambiguate via <code>condition</code> string. Found in Maximum/Absolute Maximum Ratings table.
- <code>ifsm</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>). Non-repetitive peak surge current. Single SpecValue typically. Condition carries pulse width or half-cycle description.
- <code>vr_max</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Emit separate SpecValues for VRRM (peak repetitive), VR (DC blocking), and VRWM (working peak inverse) when all three are specified. Found in Maximum Ratings table.
- <code>breakdown_voltage</code>: SpecValue list (<code>min</code>, <code>unit: "V"</code>). Reverse avalanche breakdown V_BR — distinct from vr_max (rated working voltage). Found in Electrical Characteristics. Null when not explicitly specified (many power Schottky diodes omit it).
- <code>ir</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>). Reverse leakage current. Multiple SpecValues for different reverse voltage and junction temperature test conditions. Found in Electrical Characteristics.
- <code>trr</code>: SpecValue list (<code>max</code>, <code>unit: "s"</code> — seconds, NOT nanoseconds). Reverse recovery time. Found in Dynamic / Switching Characteristics. Null for slow rectifiers and zeners.
- <code>cd</code>: SpecValue list (<code>max</code>, <code>unit: "F"</code> — farads, NOT picofarads). Junction capacitance. Condition carries Vr and test frequency. Most relevant for varicap, signal, and switching diodes.
- <code>vz</code>: SpecValue list (<code>typ</code>, <code>unit: "V"</code>). Zener breakdown voltage. Null for all non-zener diode types. Condition carries Iz test current.
- <code>power_dissipation</code>: SpecValue list (<code>max</code>, <code>unit: "W"</code>). Continuous power dissipation. Condition carries Ta or TL (e.g. <code>"TL≤25°C"</code>, <code>"TA=25°C"</code>). Found in Maximum Ratings.
- <code>tj_max</code>: SpecValue list (<code>unit: "°C"</code>). Operating junction temperature. Populate <code>min</code> with storage/operating lower bound if given, <code>max</code> with the upper limit. Found in Maximum Ratings.
- <code>thermal_resistance</code>: nested object with three nullable SpecValue-list sub-fields, all <code>unit: "K/W"</code>:
  - <code>rtheta_ja</code> — junction-to-ambient. Present for most packages; condition may specify board/pad conditions.
  - <code>rtheta_jc</code> — junction-to-case. Present for some power packages.
  - <code>rtheta_jl</code> — junction-to-lead. Common for SMD packages (SMC, SMA). Null for through-hole axial parts.
  Found in Thermal Characteristics table.
- <code>package</code>: object with <code>code</code> (string), <code>pin_count</code> (integer), <code>pitch_mm</code> (number or null), <code>body_mm</code> (nested object with <code>length</code>, <code>width</code>, <code>height</code> — all numbers in millimeters; aligns with <code>base.schema.json</code>'s body_mm shape), <code>thermal_pad</code> (boolean or null), <code>evidence</code>. Found in Package Dimensions / Mechanical Data section.
- <code>marking_code</code>: string or null. Surface marking printed on the package (e.g. <code>"V4148"</code>, <code>"B540"</code>). Found in Marking / Ordering Information section.
- <code>polarity_marking_convention</code>: string or null. How the cathode is identified on the physical part (e.g. <code>"cathode band"</code>, <code>"polarity band on plastic body"</code>, <code>"K mark"</code>, <code>"flat side = cathode"</code>).

## Hard rules

1. **Canonical SI units.** Capacitance in F (NOT pF — store 4pF as <code>4e-12</code> with <code>unit: "F"</code>). Time in s (NOT ns — store 8ns as <code>8e-9</code> with <code>unit: "s"</code>). Resistance in Ω. Voltage in V. Current in A. Power in W. Thermal resistance in K/W. The verifier rejects non-SI prefix strings.
2. **Every SpecValue requires <code>evidence</code>** with <code>page</code> (1-based integer), <code>section</code> (string or null), <code>confidence</code> (<code>"high"</code>, <code>"medium"</code>, or <code>"low"</code>), <code>method</code> (one of <code>table</code>, <code>prose</code>, <code>curve</code>, <code>calculated</code>, <code>derived</code>). Use <code>"table"</code> for values read from a parameter table; <code>"curve"</code> for values read off a graph (lower confidence); <code>"prose"</code> for values found in descriptive text; <code>"calculated"</code> for values resolved from a symbolic expression; <code>"derived"</code> for values inferred from other datasheet facts.
3. **OMIT fields you cannot find** with null. No guessing. A missing <code>trr</code> is much better than a hallucinated one.
4. **Type-specific fields.** <code>vz</code> only for zeners. <code>trr</code> typically null for slow rectifiers and zeners. <code>cd</code> most relevant for varicaps and signal/switching diodes — still populate for other types if the datasheet specifies it.
5. **Multiple test conditions = multiple SpecValues.** If <code>vf</code> is given at If=1mA AND If=10mA AND If=100mA, emit three separate SpecValue entries in the <code>vf</code> array, each with a distinct <code>condition</code> string. Same rule applies to <code>if_max</code> (continuous / average / peak) and <code>vr_max</code> (VRRM / VR / VRWM).

## Output format

Return only the JSON object. No prose, no fences. Output must validate against <code>{{SCHEMA_PATH}}</code>.

Example (MBRS540T3G — ON Semiconductor Schottky power rectifier, SMC package):

<pre><code>
{
  "diode_type": "schottky",
  "vf": [{"min": null, "typ": null, "max": 0.50, "unit": "V",
          "condition": "iF=5A, TC=25°C", "notes": null,
          "evidence": {"page": 2, "section": "Electrical Characteristics", "confidence": "high", "method": "table"}}],
  "if_max": [
    {"min": null, "typ": null, "max": 5.0, "unit": "A",
     "condition": "Average rectified, TC=105°C", "notes": null,
     "evidence": {"page": 2, "section": "Maximum Ratings", "confidence": "high", "method": "table"}},
    {"min": null, "typ": null, "max": 10.0, "unit": "A",
     "condition": "Repetitive peak, square wave 20kHz, TC=80°C", "notes": null,
     "evidence": {"page": 2, "section": "Maximum Ratings", "confidence": "high", "method": "table"}}
  ],
  "ifsm": [{"min": null, "typ": null, "max": 190, "unit": "A",
            "condition": "Halfwave single phase 60Hz surge", "notes": null,
            "evidence": {"page": 2, "section": "Maximum Ratings", "confidence": "high", "method": "table"}}],
  "vr_max": [{"min": null, "typ": null, "max": 40, "unit": "V",
              "condition": "VRRM / VRWM / VR (DC blocking)", "notes": null,
              "evidence": {"page": 2, "section": "Maximum Ratings", "confidence": "high", "method": "table"}}],
  "tj_max": [{"min": -65, "typ": null, "max": 150, "unit": "°C", "condition": null, "notes": null,
              "evidence": {"page": 2, "section": "Maximum Ratings", "confidence": "high", "method": "table"}}],
  "thermal_resistance": {
    "rtheta_ja": [{"min": null, "typ": 111, "max": null, "unit": "K/W", "condition": "Min pad", "notes": null,
                   "evidence": {"page": 2, "section": "Thermal Characteristics", "confidence": "high", "method": "table"}}],
    "rtheta_jc": null,
    "rtheta_jl": [{"min": null, "typ": 12, "max": null, "unit": "K/W", "condition": "Min pad", "notes": null,
                   "evidence": {"page": 2, "section": "Thermal Characteristics", "confidence": "high", "method": "table"}}]
  },
  "package": {
    "code": "SMC", "pin_count": 2, "pitch_mm": null,
    "body_mm": {"length": 5.9, "width": 6.875, "height": 2.28},
    "thermal_pad": false,
    "evidence": {"page": 5, "section": "Package Dimensions", "confidence": "high", "method": "table"}
  },
  "marking_code": "B540",
  "polarity_marking_convention": "polarity band on plastic body indicates cathode",
  "trr": null, "cd": null, "vz": null, "breakdown_voltage": null, "power_dissipation": null,
  "ir": [{"min": null, "typ": null, "max": 3e-4, "unit": "A", "condition": "Rated DC voltage, TC=25°C", "notes": null,
          "evidence": {"page": 2, "section": "Electrical Characteristics", "confidence": "high", "method": "table"}}]
}
</code></pre>
