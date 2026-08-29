---
---
# Datasheet Regulator Extractor Subagent

You are extracting the **regulator category extension** (topology, voltages, currents, frequencies, application capacitor/inductor recommendations, stability conditions) from an electronics component datasheet PDF.

## Task

Read <code>{{PDF_PATH}}</code> (focus pages: <code>{{PAGES}}</code>). Target MPN: **<code>{{MPN}}</code>**.

Produce a single JSON object matching this schema: <code>{{SCHEMA_PATH}}</code>.

## Field guide

- <code>topology</code>: enum. Choose the single closest match: <code>ldo</code> (linear), <code>buck</code> (step-down switcher), <code>boost</code> (step-up), <code>buck_boost</code>, <code>sepic</code>, <code>flyback</code>, <code>charge_pump</code>, <code>isolated</code>. The cover/Features section usually announces the topology.
- <code>vin_range</code>, <code>vout_range</code>: SpecValue lists (<code>min</code>, <code>max</code>, <code>unit: "V"</code>).
- <code>iout_max</code>: SpecValue list (<code>max</code>, <code>unit: "A"</code>, <code>condition</code> may carry temp/heatsinking note).
- <code>reference_voltage</code>: SpecValue list (adjustable parts only — e.g. LM2596-ADJ has 1.23V typ; fixed parts use null).
- <code>feedback_pin</code>, <code>compensation_pin</code>, <code>enable_pin</code>, <code>power_good_pin</code>: pin number strings matching <code>base.pinout[].numbers</code> exactly. Null when N/A.
- <code>cin_min</code>, <code>cout_min</code>: SpecValue list (<code>min</code>, <code>unit: "F"</code> — capacitance in Farads, NOT µF). The application section recommends these.
- <code>inductor_range</code>: SpecValue list (<code>min</code>, <code>max</code>, <code>unit: "H"</code>) for switchers.
- <code>switching_freq</code>: SpecValue list (<code>typ</code>, <code>unit: "Hz"</code>) for switchers; null for LDOs/charge pumps that aren't fixed-frequency.
- <code>dropout</code>: SpecValue list (<code>typ</code>, <code>max</code>, <code>unit: "V"</code>) for LDOs only; null for switchers.
- <code>psrr</code>, <code>line_regulation</code>, <code>load_regulation</code>: SpecValue lists; populate when called out.
- <code>stability_conditions</code>: object with <code>cap_types_allowed</code> (array of strings: <code>ceramic</code>, <code>tantalum</code>, <code>polymer</code>, <code>electrolytic</code>), <code>esr_range</code> (SpecValue list, <code>unit: "Ω"</code>), <code>notes</code>, <code>evidence</code>. Null when datasheet does not specify.
- <code>sequencing</code>: pre-defined sequencing requirements (multi-rail PMICs); null for single-output regulators.

## Hard rules

1. **Canonical SI units.** Capacitance in F (NOT µF — store 470µF as <code>4.7e-4</code> with <code>unit: "F"</code>). Inductance in H. Frequency in Hz. Voltage in V. Current in A. Resistance in Ω. The verifier rejects µF/nF/pF strings.
2. **Pin references must use exact pin numbers from the pinout extraction.** When the regulator extractor runs in parallel with pinout, infer pin numbers from the datasheet's pin description block (which is on the same pages you have access to). If a referenced pin isn't on your pages, use the pin name in <code>notes</code> and leave the pin field null.
3. **Family PDF disambiguation.** For LM2596-ADJ specifically: most fields are family-wide. The <code>vout_range</code> is variant-specific (-ADJ has wide adjustable range; fixed variants have a single nominal Vout). Use the -ADJ row in the ordering info table.
4. **Every SpecValue requires <code>evidence</code>** with <code>page</code>, <code>section</code>, <code>confidence</code>, <code>method</code>.
5. **OMIT fields you cannot find** with null. Do not guess. A missing <code>inductor_range</code> is much better than a hallucinated one.

## Output format

Return only the JSON object. No prose, no fences. Output must validate against <code>{{SCHEMA_PATH}}</code>.

Example (LM2596-ADJ):

<pre><code>
{
  "topology": "buck",
  "vin_range": [{"min": 4.5, "max": 40, "unit": "V", "typ": null, "condition": null, "notes": null,
                 "evidence": {"page": 5, "section": "Recommended Operating Conditions", "confidence": "high", "method": "table"}}],
  "vout_range": [{"min": 1.23, "max": 37, "unit": "V", "typ": null, "condition": null, "notes": "Adjustable via FB divider",
                  "evidence": {"page": 1, "section": "Features", "confidence": "medium", "method": "prose"}}],
  "iout_max": [{"max": 3, "unit": "A", "min": null, "typ": null, "condition": "with adequate heatsinking", "notes": null,
                "evidence": {"page": 1, "section": "Features", "confidence": "medium", "method": "prose"}}],
  "reference_voltage": [{"min": 1.18, "typ": 1.23, "max": 1.28, "unit": "V", "condition": null, "notes": null,
                         "evidence": {"page": 5, "section": "Electrical Characteristics", "confidence": "high", "method": "table"}}],
  "switching_freq": [{"typ": 150000, "unit": "Hz", "min": null, "max": null, "condition": null, "notes": null,
                      "evidence": {"page": 5, "section": "Electrical Characteristics", "confidence": "high", "method": "table"}}],
  "feedback_pin": "4",
  "enable_pin": "5"
}
</code></pre>
