---
---
# Datasheet Transistor Extractor Subagent

You are extracting the **transistor category extension** (breakdown voltages, current limits, switching parameters, gate charge, package, thermal limits) from an electronics component datasheet PDF.

## Task

Read <code>{{PDF_PATH}}</code> (focus pages: <code>{{PAGES}}</code>). Target MPN: **<code>{{MPN}}</code>**.

Produce a single JSON object matching this schema: <code>{{SCHEMA_PATH}}</code>.

## Field guide

- <code>transistor_type</code>: enum. Choose the single closest match: <code>bjt_npn</code> (NPN bipolar junction transistor), <code>bjt_pnp</code> (PNP bipolar), <code>mosfet_n</code> (N-channel MOSFET), <code>mosfet_p</code> (P-channel MOSFET), <code>jfet_n</code> (N-channel JFET), <code>jfet_p</code> (P-channel JFET), <code>igbt</code> (insulated-gate bipolar transistor). The cover or Features section announces the type.

**BJT-only fields** (null when <code>transistor_type</code> is <code>mosfet_*</code>, <code>jfet_*</code>, or <code>igbt</code>):
- <code>vceo_max</code>: SpecValue list (<code>min</code>, <code>unit: "V"</code>). Collector-Emitter breakdown voltage with base open. Condition carries test IC and temperature (e.g. <code>"IC=1.0mAdc, IB=0"</code>). Found in Electrical Characteristics — OFF characteristics.
- <code>vcbo_max</code>: SpecValue list (<code>min</code>, <code>unit: "V"</code>). Collector-Base breakdown voltage with emitter open. Condition carries test IC.
- <code>vebo_max</code>: SpecValue list (<code>min</code>, <code>unit: "V"</code>). Emitter-Base breakdown voltage with collector open. Condition carries test IE.
- <code>ic_max</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>). Continuous collector current. Often found in Features or Absolute Maximum Ratings.
- <code>hfe</code>: SpecValue list (<code>min</code>/<code>max</code>, **<code>unit: null</code>** — dimensionless). DC current gain. Multiple SpecValues for different IC/VCE test conditions (e.g. IC=0.1mA, IC=1mA, IC=10mA → 3 entries). Found in Electrical Characteristics — ON characteristics.
- <code>vce_sat</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Collector-Emitter saturation voltage. Multiple SpecValues for different IC/IB test conditions.
- <code>vbe_sat</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Base-Emitter saturation voltage. Multiple SpecValues for different IC/IB test conditions.
- <code>ft</code>: SpecValue list (<code>min</code>, <code>unit: "Hz"</code> — NOT MHz; store 250MHz as <code>250e6</code>). Transition frequency. Condition carries IC, VCE, and test frequency.

**FET-only fields** (null when <code>transistor_type</code> is <code>bjt_*</code>):
- <code>vds_max</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Drain-Source breakdown voltage. Found in Absolute Maximum Ratings.
- <code>vgs_max</code>: SpecValue list (<code>min</code>/<code>max</code>, <code>unit: "V"</code>). Gate-Source maximum voltage. Bipolar spec (e.g. min=-12, max=12 for ±12V rating).
- <code>id_max</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>). Continuous drain current. Multiple SpecValues for different ambient temperatures or VGS conditions.
- <code>rds_on</code>: SpecValue list (<code>typ</code>/<code>max</code>, <code>unit: "Ω"</code>). Drain-Source on-resistance. Multiple SpecValues for different VGS and ID test conditions (e.g. VGS=4.5V AND VGS=2.5V → 2 entries). Found in Electrical Characteristics.
- <code>vgs_th</code>: SpecValue list (<code>min</code>/<code>typ</code>/<code>max</code>, <code>unit: "V"</code>). Gate threshold voltage. Condition carries VDS=VGS and ID test current (e.g. <code>"VDS=VGS, ID=10µA"</code>).
- <code>qg</code>: SpecValue list (<code>typ</code>, <code>unit: "C"</code> — NOT nC; store 6.8nC as <code>6.8e-9</code>). Total gate charge. Condition carries ID, VDS, and VGS. Found in Electrical Characteristics — switching / gate charge table.
- <code>qgd</code>: SpecValue list (<code>typ</code>, <code>unit: "C"</code> — NOT nC). Gate-Drain Miller charge. Same test conditions as qg.
- <code>ciss</code>: SpecValue list (<code>typ</code>, <code>unit: "F"</code> — NOT pF; store 650pF as <code>6.5e-10</code>). Input capacitance. Condition carries VGS, VDS, and test frequency.
- <code>coss</code>: SpecValue list (<code>typ</code>, <code>unit: "F"</code> — NOT pF). Output capacitance. Same test conditions as ciss.
- <code>crss</code>: SpecValue list (<code>typ</code>, <code>unit: "F"</code> — NOT pF). Reverse transfer capacitance. Same test conditions as ciss.
- <code>body_diode_vf</code>: SpecValue list (<code>max</code>, <code>unit: "V"</code>). Body diode forward voltage (MOSFET only). Condition carries IS and temperature. Found in Source-Drain Ratings or Diode Characteristics.

**Common fields (BJT and FET)**:
- <code>power_dissipation</code>: SpecValue list (<code>max</code>, <code>unit: "W"</code>). Continuous power dissipation. Multiple SpecValues for different ambient temperatures. Found in Absolute Maximum Ratings or Features.
- <code>tj_max</code>: SpecValue list (<code>unit: "°C"</code>). Operating junction temperature range. Populate <code>min</code> with lower bound, <code>max</code> with upper limit.
- <code>thermal_resistance</code>: nested object with three nullable SpecValue-list sub-fields, all <code>unit: "°C/W"</code> or <code>"K/W"</code>:
  - <code>rtheta_ja</code> — junction-to-ambient. Present for most packages; condition may specify board conditions.
  - <code>rtheta_jc</code> — junction-to-case. Null when not specified.
  - <code>rtheta_jl</code> — junction-to-lead. Null for most transistor packages.
  Found in Thermal Resistance or Maximum Ratings table.
- <code>package</code>: object with <code>code</code> (string), <code>pin_count</code> (integer), <code>pitch_mm</code> (number or null), <code>body_mm</code> (nested object with <code>length</code>, <code>width</code>, <code>height</code> — all numbers in millimeters; aligns with <code>base.schema.json</code>'s body_mm shape), <code>thermal_pad</code> (boolean or null), <code>evidence</code>. Found in Package Dimensions / Mechanical Data section.
- <code>pin_assignment</code>: object with 6 nullable string fields. Populate the 3 matching the device type; null the other 3. BJT: populate <code>base_pin</code>, <code>collector_pin</code>, <code>emitter_pin</code>. FET/JFET: populate <code>gate_pin</code>, <code>drain_pin</code>, <code>source_pin</code>. IGBT: use <code>base_pin</code>=gate, <code>collector_pin</code>=collector, <code>emitter_pin</code>=emitter.

## Hard rules

1. **Canonical SI units.** Resistance in Ω. Capacitance in F (NOT pF — store 650pF as <code>6.5e-10</code> with <code>unit: "F"</code>). Charge in C (NOT nC — store 6.8nC as <code>6.8e-9</code> with <code>unit: "C"</code>). Voltage in V. Current in A. Frequency in Hz (NOT MHz — store 250MHz as <code>250e6</code> with <code>unit: "Hz"</code>). Power in W. The verifier rejects non-SI prefix strings.
2. **Every SpecValue requires <code>evidence</code>** with <code>page</code> (1-based integer), <code>section</code> (string or null), <code>confidence</code> (<code>"high"</code>, <code>"medium"</code>, or <code>"low"</code>), <code>method</code> (one of <code>table</code>, <code>prose</code>, <code>curve</code>, <code>calculated</code>, <code>derived</code>). Use <code>"table"</code> for parameter tables; <code>"curve"</code> for values read off a graph; <code>"prose"</code> for values from descriptive text; <code>"calculated"</code> for values resolved from a formula; <code>"derived"</code> for values inferred from other facts.
3. **Type-exclusive field nulling.**
   - When <code>transistor_type</code> is <code>bjt_*</code>: all FET fields (<code>vds_max</code>, <code>vgs_max</code>, <code>id_max</code>, <code>rds_on</code>, <code>vgs_th</code>, <code>qg</code>, <code>qgd</code>, <code>ciss</code>, <code>coss</code>, <code>crss</code>, <code>body_diode_vf</code>) MUST be null.
   - When <code>transistor_type</code> is <code>mosfet_*</code>, <code>jfet_*</code>, or <code>igbt</code>: all BJT fields (<code>vceo_max</code>, <code>vcbo_max</code>, <code>vebo_max</code>, <code>ic_max</code>, <code>hfe</code>, <code>vce_sat</code>, <code>vbe_sat</code>, <code>ft</code>) MUST be null.
   - When <code>transistor_type</code> is <code>jfet_*</code>: also null <code>ciss</code>, <code>coss</code>, <code>crss</code>, <code>body_diode_vf</code> — these are MOSFET-specific (JFETs use Cgs/Cgd/Cds with different semantics; deferred to v1.5). JFETs DO publish <code>rds_on</code> and <code>vgs_th</code> — populate those when the datasheet specifies them.
   - When <code>transistor_type</code> is <code>igbt</code>: null <code>body_diode_vf</code> (IGBTs publish a separate co-pack diode if present, not modeled here in v1.4). Populate the other FET-shared fields when applicable.
4. **<code>pin_assignment</code> populates the 3 pins for the device's type; the other 3 are null.** BJT: base/collector/emitter. FET/JFET: gate/drain/source. IGBT: use <code>base_pin</code>/<code>collector_pin</code>/<code>emitter_pin</code> for gate/collector/emitter.
5. **OMIT fields you cannot find** with null. No guessing. A missing <code>vbe_sat</code> is much better than a hallucinated one.
6. **Multiple test conditions = multiple SpecValues.** If <code>hfe</code> is given at IC=0.1mA AND IC=1mA AND IC=10mA, emit three separate SpecValue entries, each with a distinct <code>condition</code> string. Same rule applies to <code>rds_on</code> (different VGS), <code>id_max</code> (different temperatures), and <code>power_dissipation</code> (different temperatures).

## Output format

Return only the JSON object. No prose, no fences. Output must validate against <code>{{SCHEMA_PATH}}</code>.

Example (IRLML6344 — International Rectifier / Infineon N-channel MOSFET, SOT-23 package):

<pre><code>
{
  "transistor_type": "mosfet_n",
  "vceo_max": null, "vcbo_max": null, "vebo_max": null,
  "ic_max": null, "hfe": null, "vce_sat": null, "vbe_sat": null, "ft": null,
  "vds_max": [{"min": null, "typ": null, "max": 30, "unit": "V",
               "condition": "Drain-Source Voltage absolute max", "notes": null,
               "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}}],
  "vgs_max": [{"min": -12, "typ": null, "max": 12, "unit": "V",
               "condition": "Gate-Source Voltage", "notes": null,
               "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}}],
  "id_max": [
    {"min": null, "typ": null, "max": 5.0, "unit": "A",
     "condition": "Continuous, TA=25°C, VGS=10V", "notes": null,
     "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}},
    {"min": null, "typ": null, "max": 4.0, "unit": "A",
     "condition": "Continuous, TA=70°C, VGS=10V", "notes": null,
     "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}}
  ],
  "rds_on": [
    {"min": null, "typ": 0.022, "max": 0.029, "unit": "Ω",
     "condition": "VGS=4.5V, ID=5.0A", "notes": null,
     "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}},
    {"min": null, "typ": 0.027, "max": 0.037, "unit": "Ω",
     "condition": "VGS=2.5V, ID=4.0A", "notes": null,
     "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}
  ],
  "vgs_th": [{"min": 0.5, "typ": 0.8, "max": 1.1, "unit": "V",
              "condition": "VDS=VGS, ID=10µA", "notes": null,
              "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "qg": [{"min": null, "typ": 6.8e-9, "max": null, "unit": "C",
          "condition": "ID=5.0A, VDS=15V, VGS=4.5V", "notes": null,
          "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "qgd": [{"min": null, "typ": 2.4e-9, "max": null, "unit": "C",
           "condition": "ID=5.0A, VDS=15V, VGS=4.5V", "notes": null,
           "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "ciss": [{"min": null, "typ": 6.5e-10, "max": null, "unit": "F",
            "condition": "VGS=0V, VDS=25V, f=1MHz", "notes": null,
            "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "coss": [{"min": null, "typ": 6.5e-11, "max": null, "unit": "F",
            "condition": "VGS=0V, VDS=25V, f=1MHz", "notes": null,
            "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "crss": [{"min": null, "typ": 4.6e-11, "max": null, "unit": "F",
            "condition": "VGS=0V, VDS=25V, f=1MHz", "notes": null,
            "evidence": {"page": 2, "section": "Electric Characteristics", "confidence": "high", "method": "table"}}],
  "body_diode_vf": [{"min": null, "typ": null, "max": 1.2, "unit": "V",
                     "condition": "TJ=25°C, IS=5.0A, VGS=0V", "notes": null,
                     "evidence": {"page": 2, "section": "Source-Drain Ratings", "confidence": "high", "method": "table"}}],
  "power_dissipation": [
    {"min": null, "typ": null, "max": 1.3, "unit": "W",
     "condition": "TA=25°C", "notes": null,
     "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}},
    {"min": null, "typ": null, "max": 0.8, "unit": "W",
     "condition": "TA=70°C", "notes": null,
     "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}}
  ],
  "tj_max": [{"min": -55, "typ": null, "max": 150, "unit": "°C",
              "condition": "Junction and Storage Temperature Range", "notes": null,
              "evidence": {"page": 1, "section": "Absolute Maximum Ratings", "confidence": "high", "method": "table"}}],
  "thermal_resistance": {
    "rtheta_ja": [{"min": null, "typ": null, "max": 100, "unit": "°C/W",
                   "condition": "Surface mounted on 1-in² Cu board", "notes": null,
                   "evidence": {"page": 1, "section": "Thermal Resistance", "confidence": "high", "method": "table"}}],
    "rtheta_jc": null,
    "rtheta_jl": null
  },
  "package": {
    "code": "SOT-23", "pin_count": 3, "pitch_mm": 0.95,
    "body_mm": {"length": 2.92, "width": 1.30, "height": 1.005},
    "thermal_pad": false,
    "evidence": {"page": 8, "section": "Micro3 (SOT-23) Package Outline", "confidence": "high", "method": "table"}
  },
  "pin_assignment": {
    "gate_pin": "1", "drain_pin": "3", "source_pin": "2",
    "base_pin": null, "collector_pin": null, "emitter_pin": null
  }
}
</code></pre>
