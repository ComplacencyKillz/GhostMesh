---
---
# Datasheet Verification Reference

Automated cross-check of schematic connections against structured datasheet extractions. Catches pin voltage violations, missing required external components, and insufficient decoupling -- issues that manual review often misses because they require reading the datasheet for every IC.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Verification Checks](#verification-checks)
4. [Output Schema](#output-schema)
5. [Extraction Fields Used](#extraction-fields-used)
6. [Limitations](#limitations)
7. [Common User Intents](#common-user-intents)

---

## Overview

The verification bridge (<code>datasheet_verify.py</code>) compares what the schematic analyzer found (net voltages, connected components, decoupling caps) against what the datasheet says should be there (pin voltage limits, required external components, application circuit recommendations).

It runs as part of the schematic analysis pipeline. When the <code>datasheets/extracted/</code> cache directory exists and contains extraction JSON files for the design's ICs, the verifier activates automatically. When no extractions are available, it returns an empty result with a note.

**What it catches:**

- Net voltage exceeding a pin's absolute maximum rating (potential damage)
- Net voltage exceeding a pin's recommended operating range (potential malfunction)
- Missing capacitors, resistors, inductors, or diodes that the datasheet requires on specific pins
- Decoupling capacitor count or value falling short of application circuit recommendations

---

## Prerequisites

The verification pipeline has four stages. All must complete before verification can run:

### Stage 1: Download datasheets

Use <code>sync_datasheets_digikey.py</code> (or <code>fetch_datasheet_digikey.py</code> for individual parts) to download PDF datasheets for all ICs in the design. The script uses <code>analyze_schematic.py</code> to extract MPNs automatically.

<pre><code>
python3 <skill-path>/scripts/sync_datasheets_digikey.py <project_dir>
</code></pre>

PDFs are saved to <code>datasheets/</code> in the project directory.

### Stage 2: LLM extraction

The LLM reads the downloaded PDF pages and produces structured JSON for each IC. The page selector (<code>datasheet_page_selector.py</code>) identifies which pages contain pin tables, absolute maximum ratings, and application circuits. The agent then fills in the extraction schema documented in the **<code>datasheets</code> skill** — see <code>skills/datasheets/references/extraction-schema.md</code> for the canonical schema and <code>skills/datasheets/references/field-extraction-guide.md</code> for how to find each field in vendor datasheets.

This step is interactive — it requires the agent to read PDF pages and produce JSON. It cannot be fully automated.

### Stage 3: Cache extractions

Extraction JSON files are stored in <code>datasheets/extracted/</code> with filenames derived from the MPN (non-alphanumeric characters replaced with underscores). An optional <code>manifest.json</code> (legacy name <code>index.json</code>) provides case-insensitive MPN-to-file mapping.

<pre><code>
datasheets/extracted/
  TPS61023DRLR.json
  STM32F405RGT6.json
  USBLC6_2SC6.json
  manifest.json       # optional (legacy name: index.json)
</code></pre>

### Stage 4: Verification

The verifier runs automatically when <code>run_datasheet_verification()</code> is called with the schematic analysis JSON. It resolves the extraction directory by checking:

1. <code><project_dir>/datasheets/extracted/</code>
2. <code><project_dir>/../datasheets/extracted/</code>

If neither exists, verification is skipped.

---

## Verification Checks

### Pin voltage absolute maximum exceeded

**Type:** <code>pin_voltage_abs_max_exceeded</code>
**Severity:** CRITICAL
**Condition:** Net voltage > pin's <code>voltage_abs_max</code>

Compares the estimated voltage on each pin's connected net against the absolute maximum rating from the datasheet extraction. Net voltages are resolved from:

1. The top-level <code>rail_voltages</code> dict (e.g., <code>+3V3</code> -> 3.3V)
2. Name parsing heuristics: <code>+3V3</code> -> 3.3, <code>+5V</code> -> 5.0, <code>12V0</code> -> 12.0

GND pins are skipped. Pins without a <code>voltage_abs_max</code> in the extraction are skipped.

**Example finding:**

<pre><code>
U3 pin 4 (VIN) on +12V (12.0V) exceeds absolute maximum (6.0V) by 6.00V
</code></pre>

This is always CRITICAL -- exceeding absolute maximum ratings causes permanent device damage.

### Pin voltage operating range exceeded

**Type:** <code>pin_voltage_operating_exceeded</code>
**Severity:** HIGH or MEDIUM
**Condition:** Net voltage > pin's <code>voltage_operating_max</code> (but below <code>voltage_abs_max</code>)

Same net voltage resolution as above, but checks against the recommended operating maximum instead of the absolute maximum.

Severity depends on the margin to absolute maximum:
- **HIGH** when less than 10% margin to <code>voltage_abs_max</code>
- **MEDIUM** when 10% or more margin to <code>voltage_abs_max</code>

If no <code>voltage_abs_max</code> is available, the margin is treated as 0% (HIGH severity).

**Example finding:**

<pre><code>
U1 pin 2 (VDD) on +5V (5.0V) exceeds recommended operating maximum (4.5V)
</code></pre>

### Missing required external components

**Type:** <code>missing_required_external</code>
**Severity:** HIGH
**Condition:** Pin has <code>required_external</code> in extraction, but no matching component type found on the net

For each IC pin that has a <code>required_external</code> field in the extraction (e.g., "100nF bypass cap to GND", "10K pull-up to VCC"), the verifier checks whether any component of the expected type is connected to that pin's net.

The expected component type is parsed from the <code>required_external</code> text:

| Keywords in <code>required_external</code> | Expected type(s) |
|--------------------------------|-------------------|
| cap, capacitor, decoupling, bypass | <code>capacitor</code> |
| resistor, pull-up, pullup, pull-down, divider | <code>resistor</code> |
| inductor, ferrite, bead | <code>inductor</code>, <code>ferrite_bead</code> |
| diode, schottky | <code>diode</code> |

The check examines all other components connected to the same net (excluding the IC itself). If none of the connected component types match any of the expected types, a finding is generated.

If the <code>required_external</code> text cannot be parsed into any known component type, the pin is skipped.

**Example finding:**

<pre><code>
U2 pin 8 (BYPASS): datasheet requires "100nF bypass cap to GND" but none found on net BYPASS_U2
</code></pre>

### Decoupling insufficient

**Type:** <code>decoupling_insufficient</code>
**Severity:** HIGH or MEDIUM
**Condition:** Fewer matching capacitors on power pins than the application circuit recommends

Checks the <code>application_circuit</code> section of the extraction for these fields:

- <code>input_cap_recommended</code> (e.g., "10uF ceramic, X5R or X7R")
- <code>output_cap_recommended</code> (e.g., "22uF ceramic x2")
- <code>decoupling_cap</code> (e.g., "100nF per VDD pin")

For each recommendation, the verifier:

1. Parses the recommendation text to extract minimum capacitance, required count, dielectric preferences, and placement distance
2. Identifies all power pins on the IC (pins with type <code>power</code> and direction <code>input</code>, <code>output</code>, or <code>bidirectional</code>)
3. Finds all capacitors connected to those power pin nets
4. Counts capacitors whose parsed value meets at least 80% of the recommended minimum

Severity depends on how many matching caps were found:
- **HIGH** when zero matching caps found
- **MEDIUM** when some caps found but fewer than required count

**Recommendation parsing examples:**

| Text | Parsed as |
|------|-----------|
| <code>"10uF ceramic, X5R or X7R"</code> | min 10uF, count 1, dielectric [X5R, X7R] |
| <code>"22uF ceramic x2"</code> | min 22uF, count 2 |
| <code>"100nF"</code> | min 100nF, count 1 |
| <code>"4.7uF within 5mm"</code> | min 4.7uF, count 1, max distance 5mm |

The count multiplier is parsed from <code>xN</code> or <code>x N</code> suffixes. Dielectrics are recognized: X5R, X7R, X7S, C0G, NP0, X6S. Distance constraints are parsed from "within Nmm" or "< Nmm" patterns.

**Example finding:**

<pre><code>
U4 (LM2596): datasheet recommends "22uF ceramic x2" but found 1/2 matching caps on power pins
</code></pre>

---

## Output Schema

The <code>run_datasheet_verification()</code> function returns a dict with two keys:

### findings

Array of finding objects. Each finding has:

| Field | Type | Present in | Description |
|-------|------|------------|-------------|
| <code>type</code> | string | all | Finding type identifier (see check descriptions above) |
| <code>severity</code> | string | all | <code>CRITICAL</code>, <code>HIGH</code>, or <code>MEDIUM</code> |
| <code>ref</code> | string | all | Component reference (e.g., <code>U3</code>) |
| <code>mpn</code> | string | all | Manufacturer part number |
| <code>pin_number</code> | string | all | Pin number from schematic |
| <code>pin_name</code> | string | all | Pin name from extraction |
| <code>net</code> | string | all | Net name the pin connects to |
| <code>detail</code> | string | all | Human-readable description |
| <code>net_voltage_V</code> | float | voltage checks | Estimated net voltage |
| <code>abs_max_V</code> | float | voltage checks | Absolute maximum rating from datasheet |
| <code>margin_V</code> | float | abs_max | Margin (negative = violation) |
| <code>operating_max_V</code> | float | operating check | Operating maximum from datasheet |
| <code>required</code> | string | missing_external | The <code>required_external</code> text from extraction |
| <code>expected_types</code> | array | missing_external | Component types expected based on text parsing |
| <code>connected_types</code> | array | missing_external | Component types actually found on the net |
| <code>requirement_key</code> | string | decoupling | Which field the recommendation came from |
| <code>requirement_text</code> | string | decoupling | Raw recommendation text |
| <code>required_count</code> | int | decoupling | How many caps the datasheet recommends |
| <code>required_min_farads</code> | float | decoupling | Minimum capacitance per cap |
| <code>actual_count</code> | int | decoupling | How many matching caps were found |
| <code>actual_caps</code> | array | decoupling | List of <code>{ref, value}</code> for caps on power nets |

### summary

| Field | Type | Description |
|-------|------|-------------|
| <code>ics_checked</code> | int | Total ICs in the design |
| <code>ics_with_extractions</code> | int | ICs that had extraction data available |
| <code>total_findings</code> | int | Total number of findings |
| <code>by_severity</code> | object | Count per severity level (e.g., <code>{"CRITICAL": 1, "HIGH": 3}</code>) |
| <code>note</code> | string | Present only when no extraction directory was found |

**Example output:**

<pre><code>
{
  "findings": [
    {
      "type": "pin_voltage_abs_max_exceeded",
      "severity": "CRITICAL",
      "ref": "U3",
      "mpn": "TPS61023DRLR",
      "pin_number": "4",
      "pin_name": "VIN",
      "net": "+12V",
      "net_voltage_V": 12.0,
      "abs_max_V": 6.0,
      "margin_V": -6.0,
      "detail": "U3 pin 4 (VIN) on +12V (12.0V) exceeds absolute maximum (6.0V) by 6.00V"
    }
  ],
  "summary": {
    "ics_checked": 8,
    "ics_with_extractions": 5,
    "total_findings": 1,
    "by_severity": {"CRITICAL": 1}
  }
}
</code></pre>

---

## Extraction Fields Used

Mapping of which extraction JSON fields drive which verification checks.

| Extraction Field | Location | Used By |
|-----------------|----------|---------|
| <code>pins[].voltage_abs_max</code> | Pin entry | Pin voltage abs max check |
| <code>pins[].voltage_operating_max</code> | Pin entry | Pin voltage operating range check |
| <code>pins[].required_external</code> | Pin entry | Missing required external check |
| <code>pins[].type</code> | Pin entry | All checks (filters GND pins, identifies power pins) |
| <code>pins[].direction</code> | Pin entry | Decoupling check (identifies power input/output pins) |
| <code>pins[].name</code> | Pin entry | All checks (used in finding detail text) |
| <code>pins[].number</code> | Pin entry | All checks (joins extraction pins to schematic pins) |
| <code>application_circuit.input_cap_recommended</code> | Top-level | Decoupling check |
| <code>application_circuit.output_cap_recommended</code> | Top-level | Decoupling check |
| <code>application_circuit.decoupling_cap</code> | Top-level | Decoupling check |

### Schematic analysis fields consumed

| Analysis Field | Used By |
|---------------|---------|
| <code>components[].type</code> | All checks (filters to ICs only) |
| <code>components[].reference</code> | All checks (component identification) |
| <code>components[].mpn</code> | All checks (extraction file lookup) |
| <code>components[].value</code> | All checks (fallback when mpn is absent) |
| <code>components[].pin_nets</code> | All checks (pin-to-net mapping) |
| <code>components[].parsed_value</code> | Decoupling check (capacitor value comparison) |
| <code>nets[].pins</code> | Missing external + decoupling (finds connected components) |
| <code>rail_voltages</code> | Voltage checks (net voltage estimation) |

---

## Limitations

**Extraction quality is LLM-dependent.** The extraction step relies on Claude reading PDF pages and filling in structured JSON. Complex datasheets, poor PDF formatting, or unusual pin table layouts can lead to incomplete or incorrect extractions. The quality scorer (<code>datasheet_score.py</code>) catches some gaps, but subtle errors (e.g., wrong voltage assigned to a pin) are not detectable.

**Net voltage estimation is heuristic.** Voltages are resolved from <code>rail_voltages</code> (which the schematic analyzer populates for detected power rails) and from net name parsing. Nets with non-standard names or dynamically regulated voltages may not have a voltage estimate, causing those pins to be skipped.

**Only ICs are checked.** The verifier filters to components with <code>type == "ic"</code>. Discrete transistors, MOSFETs used as switches, and other non-IC components with datasheets are not verified.

**required_external parsing is keyword-based.** The verifier recognizes common component type keywords (capacitor, resistor, inductor, diode) in the <code>required_external</code> text. Unusual phrasings or component types not in the keyword list will be silently skipped.

**Capacitance matching uses 80% tolerance.** A capacitor is considered "matching" if its parsed value is at least 80% of the recommended minimum. This is deliberately loose to account for value parsing ambiguity and the common practice of using slightly smaller values in constrained layouts.

**Decoupling checks only count caps on power pin nets.** Capacitors on signal pins or dedicated bypass nets that are not directly connected to a pin marked as <code>power</code> in the extraction will not be counted.

**No negative voltage checks.** The verifier only checks whether net voltage exceeds the maximum ratings. It does not check for negative voltage violations on pins with negative absolute maximum limits (e.g., ESD clamp pins rated to -0.3V).

**Single-sheet scope.** The verifier operates on the flattened component and net lists from the schematic analysis. It does not have visibility into hierarchical sheet boundaries or conditional assembly variants.

---

## Common User Intents

| User Says | What Happens |
|-----------|-------------|
| "Verify against datasheet" | Run full verification: all checks against all ICs with available extractions |
| "Check pin voltages" | Focus on <code>pin_voltage_abs_max_exceeded</code> and <code>pin_voltage_operating_exceeded</code> findings |
| "Are my decoupling caps right" | Focus on <code>decoupling_insufficient</code> findings; compare actual vs recommended for each IC |
| "What does the datasheet say about pin X on U3" | Load extraction for U3's MPN; look up the specific pin entry and report all fields |
| "Is U3 wired correctly" | Load extraction for U3; cross-reference every pin's <code>required_external</code> against the schematic |
| "Check if any pins are overvoltaged" | Same as "check pin voltages" -- look for voltage findings |
| "What external components does U5 need" | Load extraction for U5; list all pins with <code>required_external</code> populated and compare to what is connected |
| "Are there any datasheet violations" | Run full verification and report all findings grouped by severity |
