---
---
# Diff Analysis Reference

Compare two KiCad analysis JSON files (base vs head) and report changes. Supports schematic, PCB, EMC, and SPICE analyzer outputs with auto-detection. Zero dependencies — Python 3.8+ stdlib only.

## Table of Contents

1. [Overview](#overview)
2. [CLI Reference](#cli-reference)
3. [Analyzer Types](#analyzer-types)
4. [Severity Classification](#severity-classification)
5. [Identity Matching](#identity-matching)
6. [Output Schema](#output-schema)
7. [Integration with analysis_cache](#integration-with-analysis_cache)
8. [Common User Intents](#common-user-intents)

---

## Overview

The diff pipeline has five stages:

1. **Detect type** -- Read <code>analyzer_type</code> from both JSONs (falls back to heuristic key inspection for older files). Reject if types mismatch.
2. **Dispatch** -- Route to the type-specific diff function: <code>diff_schematic</code>, <code>diff_pcb</code>, <code>diff_emc</code>, or <code>diff_spice</code>.
3. **Match by identity** -- For list-based sections (components, detections, findings, footprints), build identity maps from each side and partition into added, removed, and matched pairs.
4. **Compare values** -- For matched pairs, compare registered value fields. Numeric deltas below the threshold percentage are suppressed.
5. **Classify severity** -- Walk the diff result and assign an overall severity: <code>none</code>, <code>minor</code>, <code>major</code>, or <code>breaking</code>.

The tool operates on pre-analyzed JSON produced by <code>analyze_schematic.py</code>, <code>analyze_pcb.py</code>, <code>analyze_emc.py</code>, or the SPICE pipeline. It never re-parses source files.

**When to use it:**
- Comparing design revisions (base branch vs PR, v1 vs v2).
- Reviewing what changed after a schematic edit.
- Tracking EMC regression/improvement across iterations.
- Verifying SPICE simulation stability after component changes.
- Automated CI gating via severity threshold.

---

## CLI Reference

<pre><code>
python3 diff_analysis.py <base> <head> [options]
</code></pre>

### Positional Arguments

| Argument | Description |
|----------|-------------|
| <code>base</code> | Path to base (old) analysis JSON |
| <code>head</code> | Path to head (new) analysis JSON |

### Options

| Flag | Description |
|------|-------------|
| <code>--output FILE</code>, <code>-o FILE</code> | Write output to file instead of stdout |
| <code>--text</code> | Human-readable text output instead of JSON |
| <code>--threshold FLOAT</code> | Ignore numeric deltas below this percentage (default: <code>1.0</code>) |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid input, parse error, type mismatch, or unrecognized analyzer type |

### Planned Flags

These flags are not yet implemented but are planned for a future release:

| Flag | Description |
|------|-------------|
| <code>--analysis-dir DIR</code> | Point to an <code>analysis/</code> folder; automatically diff the two most recent runs |
| <code>--run RUN_ID</code> | Specify a particular run ID (timestamp folder) as the base |
| <code>--trend N</code> | Show severity trend across the last N runs in the analysis directory |

---

## Analyzer Types

### Schematic

Diff function: <code>diff_schematic(base, head, threshold)</code>

Compared sections:

| Section | Identity Key | Compared Fields | Source Path |
|---------|-------------|-----------------|-------------|
| Statistics | n/a (scalar paths) | <code>total_components</code>, <code>total_nets</code>, <code>unique_parts</code>, <code>total_wires</code>, <code>total_no_connects</code> | <code>statistics.*</code> |
| Components | <code>reference</code> | <code>value</code>, <code>footprint</code>, <code>mpn</code> | <code>components[]</code> |
| Signal analysis | Per-type via SIGNAL_REGISTRY | Per-type via SIGNAL_REGISTRY | <code>findings[]</code> grouped by detector via <code>group_findings_by_detection_type()</code> |
| BOM | <code>(value, footprint)</code> tuple | <code>quantity</code> | <code>bom[]</code> |
| Connectivity | JSON-serialized item | new/resolved (set diff) | <code>connectivity_issues.{single_pin_nets,floating_nets,multi_driver_nets}</code> |
| ERC warnings | <code>(type, net, message)</code> tuple | new/resolved (set diff) | <code>design_analysis.erc_warnings[]</code> |

Signal analysis is reconstructed from <code>findings[]</code> via <code>group_findings_by_detection_type()</code>, then iterates all detector types present in either base or head. For each detection type, identity and value fields come from <code>SIGNAL_REGISTRY</code> (derived from <code>detection_schema.SCHEMAS</code>). Unknown detection types fall back to <code>["reference"]</code> identity with no value fields. The diff output still uses <code>signal_analysis</code> as a key for backward compatibility with diff consumers.

### PCB

Diff function: <code>diff_pcb(base, head, threshold)</code>

| Section | Identity Key | Compared Fields | Source Path |
|---------|-------------|-----------------|-------------|
| Statistics | n/a (scalar paths) | <code>footprint_count</code>, <code>track_segments</code>, <code>via_count</code>, <code>zone_count</code>, <code>net_count</code>, <code>copper_layers_used</code>, <code>board_width_mm</code>, <code>board_height_mm</code>, <code>total_track_length_mm</code> | <code>statistics.*</code> |
| Routing completeness | n/a (scalar) | <code>routing_complete</code>, <code>unrouted_count</code> | <code>connectivity.*</code> |
| Footprints | <code>reference</code> | <code>value</code>, <code>lib_id</code>, <code>layer</code> | <code>footprints[]</code> |

### EMC

Diff function: <code>diff_emc(base, head, threshold)</code>

| Section | Identity Key | Compared Fields | Source Path |
|---------|-------------|-----------------|-------------|
| Risk score | n/a (scalar) | <code>emc_risk_score</code> | <code>summary.emc_risk_score</code> |
| Severity distribution | n/a (scalar paths) | <code>critical</code>, <code>high</code>, <code>medium</code>, <code>low</code>, <code>info</code> | <code>summary.*</code> |
| Findings | <code>rule_id::sorted(nets)::sorted(components)</code> | <code>severity</code> (new/resolved/changed) | <code>findings[]</code> |
| Per-net scores | <code>net</code> | <code>score</code> (filtered by threshold) | <code>per_net_scores[]</code> |

Per-net score changes are sorted by absolute delta (largest first).

### SPICE

Diff function: <code>diff_spice(base, head, threshold)</code>

| Section | Identity Key | Compared Fields | Source Path |
|---------|-------------|-----------------|-------------|
| Summary counts | n/a (scalar paths) | <code>pass</code>, <code>warn</code>, <code>fail</code>, <code>skip</code>, <code>total</code> | <code>summary.*</code> |
| Simulation results | <code>subcircuit_type::sorted(components)</code> | <code>status</code> (transitions annotated) | <code>simulation_results[]</code> |
| Monte Carlo concerns | <code>subcircuit_type::metric</code> | new/resolved (set diff) | <code>monte_carlo_summary.concerns[]</code> |

Status transitions from <code>pass</code> to <code>fail</code> or <code>warn</code> are annotated as regressions with up to 3 delta fields from the result.

---

## Severity Classification

Function: <code>classify_severity(analyzer_type, diff_result)</code>

Evaluation order (first match wins):

### Breaking

| Analyzer | Condition |
|----------|-----------|
| SPICE | Any <code>status_changes</code> entry where <code>base_status == "pass"</code> and <code>head_status == "fail"</code> |
| EMC | Any new finding with <code>severity == "CRITICAL"</code> |
| Schematic | Any new ERC warning (<code>erc.new_warnings</code> non-empty) |

### Major

| Condition |
|-----------|
| <code>signal_analysis</code> key present in diff |
| <code>components</code> key present in diff |
| <code>findings</code> key present in diff (EMC) |
| <code>status_changes</code> key present in diff (SPICE) |
| <code>footprints</code> with any added, removed, or modified entries (PCB) |

### Minor

| Condition |
|-----------|
| Only <code>statistics</code> key present in diff |

### None

No changes detected, or diff result is empty.

---

## Identity Matching

### SIGNAL_REGISTRY

<code>SIGNAL_REGISTRY</code> is derived at import time from <code>detection_schema.SCHEMAS</code>:

<pre><code>
SIGNAL_REGISTRY = {dt: (s.identity_fields, s.value_fields) for dt, s in _SCHEMAS.items()}
</code></pre>

Each detection type maps to <code>(identity_fields, value_fields)</code> where both are lists of dotpath strings.

**Registered detection types and their identity/value fields:**

| Detection Type | Identity Fields | Value Fields |
|---------------|-----------------|--------------|
| <code>rc_filters</code> | <code>resistor.ref</code>, <code>capacitor.ref</code> | <code>cutoff_hz</code> |
| <code>lc_filters</code> | <code>inductor.ref</code>, <code>capacitor.ref</code> | <code>resonant_hz</code> |
| <code>voltage_dividers</code> | <code>r_top.ref</code>, <code>r_bottom.ref</code> | <code>ratio</code>, <code>vout_estimated</code> |
| <code>feedback_networks</code> | <code>r_top.ref</code>, <code>r_bottom.ref</code> | <code>ratio</code> |
| <code>opamp_circuits</code> | <code>reference</code> | <code>gain</code>, <code>gain_dB</code>, <code>configuration</code> |
| <code>crystal_circuits</code> | <code>reference</code> | <code>frequency</code>, <code>effective_load_pF</code> |
| <code>current_sense</code> | <code>shunt.ref</code> | <code>max_current_50mV_A</code>, <code>max_current_100mV_A</code> |
| <code>power_regulators</code> | <code>ref</code> | <code>vout_estimated</code>, <code>topology</code> |
| <code>transistor_circuits</code> | <code>reference</code> | <code>type</code> |
| <code>protection_devices</code> | <code>reference</code>, <code>type</code> | <code>protected_net</code> |
| <code>bridge_circuits</code> | <code>topology</code> | (none) |
| <code>rf_matching</code> | <code>antenna_ref</code> | (none) |
| <code>bms_systems</code> | <code>bms_reference</code> | <code>cell_count</code> |
| <code>decoupling_analysis</code> | <code>rail_net</code> | (none) |
| <code>rf_chains</code> | (none) | (none) |
| <code>ethernet_interfaces</code> | <code>phy_ref</code> | (none) |
| <code>memory_interfaces</code> | <code>type</code> | (none) |
| <code>isolation_barriers</code> | <code>isolator_ref</code> | (none) |
| <code>snubber_circuits</code> | (none) | (none) |

### Dotpath Resolution

Identity and value fields use dotted paths (e.g., <code>r_top.ref</code>) resolved by <code>_resolve()</code>. Each segment indexes into nested dicts. Returns <code>None</code> if any segment is missing.

### Identity Key Building

<code>_identity_key(item, fields)</code> extracts the value at each dotpath and joins them with <code>::</code>. List values are sorted and joined with <code>|</code>. If any field resolves to <code>None</code>, the entire key is <code>None</code> and the item is excluded from matching.

Example: for a voltage divider with <code>r_top.ref = "R1"</code> and <code>r_bottom.ref = "R2"</code>, the identity key is <code>R1::R2</code>.

### Generic Fallback

When a detection type is not in <code>SIGNAL_REGISTRY</code>, <code>_generic_identity()</code> is used. It tries:

1. Top-level <code>reference</code> or <code>ref</code> field.
2. Any nested dict with a <code>ref</code> sub-key.

Returns <code>None</code> if nothing is found (item is excluded from matching).

### Validation

<code>validate_signal_registry(sample_output)</code> checks that every key in <code>SIGNAL_REGISTRY</code> has at least one finding with a matching detector in <code>findings[]</code>. Returns warning strings for any missing keys. Useful for catching stale registry entries after schema changes.

---

## Output Schema

### Top-Level Structure

<pre><code>
{
  "diff_version": "1.0",
  "analyzer_type": "schematic|pcb|emc|spice",
  "base_file": "/path/to/base.json",
  "head_file": "/path/to/head.json",
  "has_changes": true,
  "summary": {
    "total_changes": 5,
    "added": 2,
    "removed": 1,
    "modified": 2,
    "severity": "major"
  },
  "diff": { ... }
}

</code></pre>

### Summary Counts

<code>summary.total_changes</code> = <code>added + removed + modified</code>. What counts as added/removed/modified depends on analyzer type:

| Analyzer | Added | Removed | Modified |
|----------|-------|---------|----------|
| Schematic | New components + new detections | Removed components + removed detections | Changed components + changed detections |
| PCB | New footprints | Removed footprints | Changed footprints |
| EMC | New findings | Resolved findings | Severity-changed findings |
| SPICE | New simulation results | Removed simulation results | Status-changed results |

### Diff Section (by analyzer type)

The <code>diff</code> object contains only sections with actual changes. Empty sections are omitted.

**Schematic diff keys:** <code>statistics</code>, <code>components</code>, <code>signal_analysis</code>, <code>bom</code>, <code>connectivity</code>, <code>erc</code>

**PCB diff keys:** <code>statistics</code>, <code>routing_complete</code>, <code>unrouted</code>, <code>footprints</code>

**EMC diff keys:** <code>risk_score</code>, <code>by_severity</code>, <code>findings</code>, <code>per_net_scores</code>

**SPICE diff keys:** <code>summary</code>, <code>status_changes</code>, <code>new_results</code>, <code>removed_results</code>, <code>monte_carlo</code>

### List Diff Format

All list-based sections (components, signal analysis detections, footprints) use the same structure:

<pre><code>
{
  "added": [{ "reference": "R5", "value": "10k", ... }],
  "removed": [{ "reference": "R3", "value": "4.7k", ... }],
  "modified": [{
    "identity": "R1/R2",
    "changes": [{
      "field": "ratio",
      "base": 0.5,
      "head": 0.33,
      "delta_pct": -34.0
    }]
  }],
  "unchanged_count": 12
}
</code></pre>

The <code>delta_pct</code> field is only present for numeric comparisons where the base value is nonzero.

### Text Output

The <code>--text</code> flag renders a summary header followed by per-section detail. Items are capped at <code>MAX_TEXT_ITEMS</code> (20) with a "... and N more changes" footer. Per-section caps: 5 items for components/footprints/findings, 3 items for signal analysis detections per type.

Format:

<pre><code>
Design Changes: schematic (major) — 5 changes
  +2 added, -1 removed, ~2 modified

Components:
  + R5 10k 0402
  - R3 4.7k 0603
  ~ R1: value 10k → 4.7k

Signal Analysis:
  + New Voltage Dividers: r_top_ref=R5 r_bottom_ref=R6
  ~ Rc Filters R1/C3: cutoff_hz 1591.55 → 3386.28
</code></pre>

---

## Integration with analysis_cache

<code>analysis_cache.should_create_new_run()</code> uses diff_analysis programmatically to decide whether new analyzer outputs warrant a new timestamped run folder.

**Protocol:**

1. Import <code>diff_analysis</code> (adding the scripts directory to <code>sys.path</code> if needed).
2. For each output type present in both the current run and the new outputs, load both JSONs.
3. Read <code>analyzer_type</code> from the base JSON and dispatch to the matching diff function (<code>diff_schematic</code>, <code>diff_pcb</code>, <code>diff_emc</code>, <code>diff_spice</code>).
4. Call <code>classify_severity()</code> on the diff result.
5. If any severity meets or exceeds the configured threshold (default: <code>major</code>), return <code>True</code> (create new run).
6. If no current run exists, return <code>True</code> (first run).
7. If all diffs are below threshold, return <code>False</code> (overwrite current run).

The threshold comparison uses a severity ordering: <code>none=0</code>, <code>minor=1</code>, <code>major=2</code>, <code>breaking=3</code>.

---

## Common User Intents

Natural-language queries and their corresponding command invocations.

| User Says | Command |
|-----------|---------|
| "What changed between these two analyses" | <code>diff_analysis.py old.json new.json --text</code> |
| "Show me changes as JSON" | <code>diff_analysis.py old.json new.json</code> |
| "Ignore small changes" | <code>diff_analysis.py old.json new.json --threshold 5.0 --text</code> |
| "Compare my schematic revisions" | <code>diff_analysis.py base.json head.json --text</code> |
| "Did the EMC risk get worse" | <code>diff_analysis.py emc_old.json emc_new.json --text</code> |
| "Any SPICE regressions" | <code>diff_analysis.py spice_old.json spice_new.json --text</code> |
| "Save the diff report" | <code>diff_analysis.py base.json head.json --output diff.json</code> |
| "Diff my last two runs" | <code>diff_analysis.py --analysis-dir analysis/ --text</code> (planned) |
| "Show trends over time" | <code>diff_analysis.py --analysis-dir analysis/ --trend 5 --text</code> (planned) |
