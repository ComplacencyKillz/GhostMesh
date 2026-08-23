---
---
# What-If Parameter Sweep Reference

Interactive parameter sweep for KiCad designs. Patches component values in analyzer JSON, recalculates affected subcircuit fields, and shows before/after impact. Supports single changes, multi-point sweeps, tolerance corner analysis, inverse fix suggestions, EMC impact preview, and PCB parasitic awareness.

## Table of Contents

1. [Overview](#overview)
2. [CLI Reference](#cli-reference)
3. [Value Formats](#value-formats)
4. [Fix Suggestions](#fix-suggestions)
5. [E-Series Snapping](#e-series-snapping)
6. [EMC Impact Preview](#emc-impact-preview)
7. [PCB Parasitic Awareness](#pcb-parasitic-awareness)
8. [Recalculable Fields](#recalculable-fields)
9. [JSON Output Schema](#json-output-schema)
10. [Common User Intents](#common-user-intents)
11. [Combinability](#combinability)

---

## Overview

The what-if pipeline operates in three stages:

1. **Patch** -- Locate all subcircuit detections in <code>findings[]</code> (grouped by detector) that reference the changed component(s) and replace their stored values.
2. **Recalculate** -- Re-derive dependent fields (cutoff frequency, divider ratio, opamp gain, etc.) using the formulas in <code>_recalc_derived()</code>.
3. **Compare** -- Diff the original and patched detections, report before/after values with percentage deltas.

The tool operates on analyzer JSON produced by <code>analyze_schematic.py</code>. It never re-parses the schematic file -- it works entirely on the pre-analyzed data.

**When to use it:**
- Exploring component value trade-offs before committing to a design change.
- Answering "what if I change R5 to 4.7k" style questions instantly.
- Finding the right component value to hit a target spec (--fix mode).
- Evaluating tolerance spread impact on derived parameters.
- Previewing EMC consequences of a component change before re-running the full EMC suite.

---

## CLI Reference

<pre><code>
python3 what_if.py <input> [changes...] [options]
</code></pre>

### Positional Arguments

| Argument | Description |
|----------|-------------|
| <code>input</code> | Analyzer JSON file (from <code>analyze_schematic.py</code>) |
| <code>changes</code> | Zero or more <code>REF=VALUE</code> pairs (e.g., <code>R5=4.7k C3=22n</code>) |

### Options

| Flag | Description |
|------|-------------|
| <code>--spice</code> | Re-run SPICE simulations on affected subcircuits (requires ngspice/LTspice/Xyce) |
| <code>--output FILE</code>, <code>-o FILE</code> | Write patched analysis JSON to file (for downstream EMC, thermal, or diff analysis) |
| <code>--text</code> | Human-readable text output instead of JSON |
| <code>--emc</code> | Show EMC impact preview (runs <code>analyze_emc.py</code> on original and patched JSON) |
| <code>--pcb FILE</code> | PCB analysis JSON for parasitic awareness (auto-discovered if omitted) |
| <code>--fix TYPE[INDEX]</code> | Inverse-solve for component values to hit a target (e.g., <code>--fix voltage_dividers[0]</code>) |
| <code>--target VALUE</code> | Target value for <code>--fix</code> mode (e.g., <code>3.3</code> for ratio, <code>1000</code> for Hz) |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid input, parse error, or missing data |

---

## Value Formats

### Single Value

<pre><code>
R5=4.7k
C3=22n
L1=10u
</code></pre>

Standard engineering notation. The parser uses <code>parse_value()</code> from <code>kicad_utils.py</code> with automatic component type detection based on the reference prefix (<code>C</code> -> capacitor, <code>L</code> -> inductor, everything else -> resistor by default).

### Comma Sweep

<pre><code>
R5=1k,2.2k,4.7k,10k
</code></pre>

Evaluates the circuit at each listed value. Results are formatted as a markdown table in <code>--text</code> mode. Only one component may use sweep syntax per invocation.

### Log-Range Sweep

<pre><code>
R5=1k..100k:10
</code></pre>

Generates <code>N</code> logarithmically spaced values between start and stop (inclusive). The step count is capped at 50.

**Syntax:** <code>START..STOP:N</code>

The log distribution is computed as: <code>v[i] = start * (stop/start)^(i/(N-1))</code>

### Tolerance Suffix

<pre><code>
R5=4.7k+-5%
R5=4.7k±5%
</code></pre>

Both <code>+-</code> and the Unicode <code>±</code> character are accepted. The tolerance triggers worst-case corner analysis: all 2^N combinations of each toleranced component at its +tol and -tol extremes. Capped at 6 components (64 corners).

Default tolerances when the suffix is omitted but tolerance mode is active:

| Prefix | Default Tolerance |
|--------|-------------------|
| <code>C</code>, <code>VC</code> | 10% |
| <code>L</code> | 20% |
| All others | 5% |

### Combined Formats

Sweep and tolerance can be combined on a single component:

<pre><code>
R5=1k,2.2k,4.7k+-5%
</code></pre>

This sweeps through the listed values and also computes tolerance corners at each step.

Multiple non-sweep changes can be specified alongside a single sweep:

<pre><code>
R5=1k,2.2k,4.7k C3=22n
</code></pre>

Here <code>C3</code> is held fixed at 22nF while <code>R5</code> sweeps.

---

## Fix Suggestions

The <code>--fix</code> mode runs an inverse solver to find component values that achieve a target specification.

### Syntax

<pre><code>
python3 what_if.py analysis.json --fix TYPE[INDEX] --target VALUE
</code></pre>

Where <code>TYPE[INDEX]</code> references a detection type and index (e.g., <code>voltage_dividers[0]</code>, <code>rc_filters[2]</code>). Internally, findings are grouped by detector name.

### Target Inference

When <code>--target</code> is omitted, the solver attempts to infer the target from the detection context:

| Detection Type | Inferred Target |
|---------------|-----------------|
| <code>voltage_dividers</code>, <code>feedback_networks</code> | <code>ratio = regulator_vref / target_vout</code> (from detection metadata) |
| <code>crystal_circuits</code> | <code>effective_load_pF = target_load_pF</code> (from detection metadata) |
| All others | Error -- <code>--target</code> is required |

### Inverse Solver Formulas

For each detection type, the solver holds one component fixed and computes the ideal value for the other. Both directions are reported as separate suggestions.

**voltage_dividers / feedback_networks** (target: <code>ratio</code>)

| Solve For | Formula | Equation ID |
|-----------|---------|-------------|
| R_bottom (fix R_top) | <code>R_bot = R_top * ratio / (1 - ratio)</code> | EQ-WI-001 |
| R_top (fix R_bottom) | <code>R_top = R_bot * (1 - ratio) / ratio</code> | EQ-WI-002 |

**rc_filters** (target: <code>cutoff_hz</code>)

| Solve For | Formula | Equation ID |
|-----------|---------|-------------|
| C (fix R) | <code>C = 1 / (2*pi*R*f_c)</code> | EQ-WI-003 |
| R (fix C) | <code>R = 1 / (2*pi*C*f_c)</code> | EQ-WI-004 |

**lc_filters** (target: <code>resonant_hz</code>)

| Solve For | Formula | Equation ID |
|-----------|---------|-------------|
| C (fix L) | <code>C = 1 / ((2*pi*f_0)^2 * L)</code> | EQ-WI-005 |
| L (fix C) | <code>L = 1 / ((2*pi*f_0)^2 * C)</code> | EQ-WI-006 |

**opamp_circuits** (target: <code>gain</code> or <code>gain_dB</code>)

When <code>gain_dB</code> is the target field, it is converted to linear gain first: <code>gain = 10^(gain_dB/20)</code>.

| Configuration | Formula | Equation ID |
|--------------|---------|-------------|
| Non-inverting | <code>R_f = R_i * (|gain| - 1)</code> | EQ-WI-007 |
| Inverting / default | <code>R_f = R_i * |gain|</code> | EQ-WI-008 |

**crystal_circuits** (target: <code>effective_load_pF</code>)

| Solve For | Formula | Equation ID |
|-----------|---------|-------------|
| Each load cap (symmetric) | <code>C_load = 2 * (target_pF - C_stray)</code> | EQ-WI-009 |

Default stray capacitance: 3.0 pF.

**current_sense** (target: <code>max_current_100mV_A</code> or <code>max_current_50mV_A</code>)

| Target | Formula | Equation ID |
|--------|---------|-------------|
| <code>max_current_100mV_A</code> | <code>R_shunt = 0.100 / I_target</code> | EQ-WI-010 |
| <code>max_current_50mV_A</code> | <code>R_shunt = 0.050 / I_target</code> | EQ-WI-011 |

### Output

Each suggestion includes the ideal (exact) value plus E-series snapped alternatives at E12, E24, and E96 with error percentage. If PCB analysis is available, footprint compatibility warnings are generated for capacitor values that may exceed the package size limit.

---

## E-Series Snapping

All fix suggestions are snapped to standard E-series values using <code>snap_to_e_series()</code> from <code>kicad_utils.py</code>.

**Algorithm:**
1. Extract the decade: <code>decade = 10^floor(log10(value))</code>
2. Normalize: <code>normalized = value / decade</code>
3. Find the closest value in the series decade list.
4. Reconstruct: <code>snapped = best * decade</code>
5. Compute error: <code>error_pct = (snapped - value) / value * 100</code>

**Available series:**

| Series | Values per Decade | Typical Tolerance |
|--------|-------------------|-------------------|
| E12 | 12 | 10% |
| E24 | 24 | 5% |
| E96 | 96 | 1% |

All three series are reported for every fix suggestion, allowing the user to choose based on availability and precision requirements.

---

## EMC Impact Preview

The <code>--emc</code> flag runs the full EMC analyzer (<code>analyze_emc.py</code>) on both the original and patched analysis JSON, then diffs the results.

### Protocol

1. The patched analysis JSON is written to a temporary file.
2. <code>analyze_emc.py</code> is invoked as a subprocess with <code>--schematic</code> pointing to each temporary file.
3. If <code>--pcb</code> is specified, it is passed through as well.
4. A 30-second timeout is enforced per invocation.
5. Temporary files are cleaned up regardless of outcome.

### Output Fields

| Field | Type | Description |
|-------|------|-------------|
| <code>before_risk</code> | string | Overall risk level before change |
| <code>after_risk</code> | string | Overall risk level after change |
| <code>resolved</code> | array | Findings that disappeared after the change |
| <code>improved</code> | array | Findings whose risk level decreased |
| <code>new_findings</code> | array | Findings that appeared after the change |
| <code>unchanged</code> | integer | Count of findings with no change |

Text mode renders this as a summary with per-finding detail.

---

## PCB Parasitic Awareness

When PCB analysis data is available, the tool annotates each affected subcircuit with trace resistance and inductance estimates.

### Providing PCB Data

1. **Explicit:** <code>--pcb pcb_analysis.json</code>
2. **Auto-discovery:** If the schematic JSON is at <code>analysis/schematic/foo.json</code>, the tool looks for <code>analysis/pcb/*.json</code> automatically.

### Trace Parasitic Formulas

Trace resistance (EQ-WI-012):

<pre><code>
R_trace = rho * length / (width * thickness)
</code></pre>

Where <code>rho</code> = 1.72e-8 ohm-m (copper), <code>thickness</code> = 35e-6 m (1 oz copper).

Trace inductance (EQ-WI-013, valid when length > width):

<pre><code>
L_trace = 2e-7 * length * ln(2 * length / width)
</code></pre>

Both are computed per net segment and summed for all track segments connected to the component.

### Footprint Compatibility

For capacitor fix suggestions, the tool checks the suggested value against typical maximum capacitance for common package sizes:

| Package | Typical Max (ceramic MLCC) |
|---------|---------------------------|
| 0402 | 100 nF |
| 0603 | 1 uF |
| 0805 | 10 uF |
| 1206 | 22 uF |
| 1210 | 47 uF |

A warning is emitted when a suggested E-series value exceeds the package limit.

---

## Recalculable Fields

The recalculation engine (<code>_recalc_derived</code> in <code>spice_tolerance.py</code>) updates these fields after patching component values:

| Detection Type | Field | Formula | Equation ID |
|---------------|-------|---------|-------------|
| <code>rc_filters</code> | <code>cutoff_hz</code> | <code>1 / (2*pi*R*C)</code> | EQ-RC-001 |
| <code>voltage_dividers</code>, <code>feedback_networks</code> | <code>ratio</code> | <code>R_bot / (R_top + R_bot)</code> | EQ-VD-001 |
| <code>lc_filters</code> | <code>resonant_hz</code> | <code>1 / (2*pi*sqrt(L*C))</code> | EQ-LC-001 |
| <code>lc_filters</code> | <code>impedance_ohms</code> | <code>sqrt(L/C)</code> | EQ-LC-002 |
| <code>crystal_circuits</code> | <code>effective_load_pF</code> | <code>(C1*C2)/(C1+C2) * 1e12 + C_stray</code> | EQ-XL-001 |
| <code>opamp_circuits</code> (inverting) | <code>gain</code> | <code>-R_f / R_i</code> | EQ-OA-001 |
| <code>opamp_circuits</code> (non-inverting) | <code>gain</code> | <code>1 + R_f / R_i</code> | EQ-OA-002 |
| <code>opamp_circuits</code> | <code>gain_dB</code> | <code>20 * log10(|gain|)</code> | EQ-OA-003 |
| <code>current_sense</code> | <code>max_current_50mV_A</code> | <code>0.050 / R_shunt</code> | EQ-CS-001 |
| <code>current_sense</code> | <code>max_current_100mV_A</code> | <code>0.100 / R_shunt</code> | EQ-CS-002 |
| <code>power_regulators</code> (feedback divider) | <code>ratio</code> | <code>R_bot / (R_top + R_bot)</code> | EQ-VD-001 |

The comparison engine also checks for any additional fields present in the detection that were not explicitly registered (e.g., <code>estimated_vout</code>).

---

## JSON Output Schema

### Single-Value Mode

<pre><code>
{
  "changes": {
    "R5": {
      "before": 10000.0,
      "after": 4700.0,
      "before_str": "10k",
      "after_str": "4.7k",
      "unit": "ohms"
    }
  },
  "affected_subcircuits": [
    {
      "type": "voltage_dividers",
      "label": "voltage divider R5/R6",
      "components": ["R5", "R6"],
      "delta": [
        {"field": "ratio", "before": 0.5, "after": 0.6808, "delta_pct": 36.2}
      ],
      "before": {"ratio": 0.5},
      "after": {"ratio": 0.6808},
      "parasitics": {},
      "tolerance": [],
      "spice_delta": {}
    }
  ],
  "summary": {
    "components_changed": 1,
    "subcircuits_affected": 1,
    "spice_verified": false
  },
  "emc_delta": null
}
</code></pre>

The <code>parasitics</code>, <code>tolerance</code>, <code>spice_delta</code>, and <code>emc_delta</code> fields are only present when the corresponding options are active.

### Sweep Mode

<pre><code>
{
  "ref": "R5",
  "values": [1000.0, 2200.0, 4700.0, 10000.0],
  "value_strs": ["1k", "2.2k", "4.7k", "10k"],
  "results": [
    {
      "value": 1000.0,
      "value_str": "1k",
      "affected_subcircuits": [
        {
          "type": "voltage_dividers",
          "label": "voltage divider R5/R6",
          "delta": [{"field": "ratio", "before": 0.5, "after": 0.909}],
          "after": {"ratio": 0.909}
        }
      ]
    }
  ]
}
</code></pre>

### Fix Mode

<pre><code>
{
  "fix_suggestions": [
    {
      "detection_type": "voltage_dividers",
      "detection_index": 0,
      "target_field": "ratio",
      "target_value": 0.3,
      "suggestions": [
        {
          "ref": "R6",
          "field": "ohms",
          "current": 10000.0,
          "ideal": 4285.7,
          "anchor_ref": "R5",
          "anchor_value": 10000.0,
          "e_series": {
            "E12": {"value": 3900.0, "error_pct": -9.0},
            "E24": {"value": 4300.0, "error_pct": 0.3},
            "E96": {"value": 4320.0, "error_pct": 0.8}
          }
        }
      ],
      "footprint_warnings": []
    }
  ]
}
</code></pre>

### Tolerance Fields (within affected_subcircuits)

<pre><code>
{
  "tolerance": [
    {
      "field": "cutoff_hz",
      "nominal": 1591.55,
      "worst_low": 1447.77,
      "worst_high": 1768.39,
      "spread_pct": 20.1
    }
  ]
}
</code></pre>

---

## Common User Intents

Natural-language queries and their corresponding command invocations.

| User Says | Command |
|-----------|---------|
| "What if I change R5 to 4.7k" | <code>what_if.py analysis.json R5=4.7k --text</code> |
| "Sweep R5 through some standard values" | <code>what_if.py analysis.json R5=1k,2.2k,4.7k,10k --text</code> |
| "Sweep R5 from 1k to 100k" | <code>what_if.py analysis.json R5=1k..100k:10 --text</code> |
| "What's the tolerance spread on this filter" | <code>what_if.py analysis.json R5=10k+-5% C3=100n+-10% --text</code> |
| "What value gives me 3.3V on this divider" | <code>what_if.py analysis.json --fix voltage_dividers[0] --target 3.3 --text</code> |
| "Fix the crystal load capacitance" | <code>what_if.py analysis.json --fix crystal_circuits[0] --text</code> (target inferred) |
| "How does changing C3 affect EMC" | <code>what_if.py analysis.json C3=1u --emc --text</code> |
| "What if I use a 4.7k with 1% tolerance instead" | <code>what_if.py analysis.json R5=4.7k+-1% --text</code> |
| "Change R5 and C3 together, show me the filter response" | <code>what_if.py analysis.json R5=4.7k C3=22n --text</code> |
| "Export the patched design for EMC analysis" | <code>what_if.py analysis.json R5=4.7k --output patched.json</code> |
| "Verify with SPICE" | <code>what_if.py analysis.json R5=4.7k --spice --text</code> |
| "What's the best R value for 1kHz cutoff" | <code>what_if.py analysis.json --fix rc_filters[0] --target 1000 --text</code> |
| "Set the opamp gain to 20 dB" | <code>what_if.py analysis.json --fix opamp_circuits[0] --target 20 --text</code> (target field = <code>gain_dB</code>) |
| "What shunt resistor for 5A max" | <code>what_if.py analysis.json --fix current_sense[0] --target 5 --text</code> |

For fix mode, the <code>--target</code> value is in the natural unit of the first derived field for that detection type (ratio for dividers, Hz for filters, linear gain or dB for opamps, pF for crystals, amps for current sense).

---

## Combinability

Which flags and modes work together:

| Combination | Supported | Notes |
|-------------|-----------|-------|
| Single change + <code>--text</code> | Yes | Primary use case |
| Single change + <code>--spice</code> | Yes | Runs SPICE on original and patched |
| Single change + <code>--emc</code> | Yes | Full EMC diff |
| Single change + <code>--pcb</code> | Yes | Adds parasitic annotations |
| Single change + <code>--output</code> | Yes | Exports patched JSON |
| Single change + tolerance | Yes | Corner analysis on toleranced components |
| Sweep + <code>--text</code> | Yes | Markdown table output |
| Sweep + tolerance | Yes | Tolerance corners at each sweep point |
| Sweep + fixed changes | Yes | Other components held at specified values |
| Sweep + <code>--spice</code> | No | Sweep mode does not run SPICE |
| Sweep + <code>--emc</code> | No | Sweep mode exits before EMC |
| Sweep + <code>--output</code> | No | Sweep mode exits before export |
| <code>--fix</code> + <code>--target</code> | Yes | Primary fix use case |
| <code>--fix</code> (no <code>--target</code>) | Partial | Only works for detection types with inferrable targets |
| <code>--fix</code> + <code>--pcb</code> | Yes | Adds footprint compatibility warnings for capacitors |
| <code>--fix</code> + changes | No | Fix mode ignores positional changes |
| <code>--fix</code> + <code>--spice</code> | No | Fix mode exits before SPICE |
| <code>--fix</code> + <code>--emc</code> | No | Fix mode exits before EMC |
| <code>--emc</code> + <code>--pcb</code> | Yes | PCB data passed through to EMC analyzer |
| Multiple changes (no sweep) | Yes | All changed components patched simultaneously |
| Multiple sweeps | No | Only one component may use sweep syntax |
