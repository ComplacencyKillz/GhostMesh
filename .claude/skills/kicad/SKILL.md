---
name: kicad
description: >-
  Analyze KiCad projects and PDF schematics: schematics, PCB layouts, Gerbers,
  footprints, symbols, netlists, and design rules. Reviews designs for bugs,
  traces nets, cross-references schematic to PCB, extracts BOM data, checks
  DRC/ERC, DFM, power trees, and regulator circuits. Every finding carries a
  confidence label and evidence source with trust_summary rollup. Analyzes PDF
  schematics from dev boards, reference designs, eval kits, and datasheets.
  Supports KiCad 5–10. Use whenever the user mentions .kicad_sch, .kicad_pcb,
  .kicad_pro, PCB design review, schematic analysis, PDF schematics, reference
  designs, Gerber files, DRC/ERC, netlist issues, BOM extraction, signal
  tracing, power budget, DFM, or wants to understand, debug, compare, or
  review any hardware design. Also for "check my board", "review before fab",
  "what's wrong with my schematic", "is this ready to order", "check my power
  supply", "verify this circuit", OSHWA certification readiness, or any
  electronics/PCB design question.
---

# KiCad Project Analysis Skill

## Related Skills

| Skill | Purpose |
|-------|---------|
| <code>bom</code> | BOM extraction, enrichment, ordering, and export workflows |
| <code>digikey</code> | Search DigiKey for parts (prototype sourcing) |
| <code>mouser</code> | Search Mouser for parts (secondary prototype source) |
| <code>lcsc</code> | Search LCSC for parts (production sourcing, JLCPCB) |
| <code>element14</code> | Search Newark/Farnell/element14 (international sourcing, reliable datasheets) |
| <code>jlcpcb</code> | PCB fabrication & assembly ordering |
| <code>pcbway</code> | Alternative PCB fabrication & assembly |
| <code>spice</code> | SPICE simulation verification of detected subcircuits |
| <code>emc</code> | EMC pre-compliance risk analysis — consumes schematic + PCB analyzer output |

**Handoff guidance:** Use this skill to parse schematics/PCBs and extract structured data. Hand off to <code>bom</code> for BOM enrichment, pricing, and ordering. Hand off to <code>digikey</code>/<code>mouser</code>/<code>lcsc</code>/<code>element14</code> for part searches and datasheet fetching. Hand off to <code>jlcpcb</code>/<code>pcbway</code> for fabrication ordering and DFM rule validation. **Always run <code>spice</code>** for simulation verification during design reviews when any SPICE simulator is installed (check with <code>which ngspice ltspice xyce</code>). **Always run <code>emc</code>** for EMC pre-compliance risk analysis during design reviews when both schematic and PCB analysis are available. These are not optional — skipping them leaves value-computation errors and EMC risks undetected.

**Before analysis:** When the user asks to analyze or review a KiCad project, check whether a <code>datasheets/</code> directory exists in the project. If not, and DigiKey API keys are available (<code>DIGIKEY_CLIENT_ID</code>), offer to sync datasheets first: "I can download datasheets for your components before analysis — this enables pin-level verification and decoupling validation against manufacturer specs. Want me to sync them?" If the user declines or no API keys are set, proceed without datasheets — the analysis works without them but datasheet verification findings won't be available.

**If you see a <code>DS-001</code> finding in the analyzer output** (severity <code>high</code>, detector <code>audit_datasheet_coverage</code>), the review cannot make any verified claim. Stop and either (a) run the datasheet sync via <code>digikey</code> / <code>mouser</code> / <code>lcsc</code> / <code>element14</code> (whichever has credentials/stock), (b) populate MPNs on the BOM parts, or (c) state explicitly in the report that every pin-level, electrical, and regulator finding is *consistency only* — do not use the words "verified", "confirmed", or "per datasheet" anywhere. <code>DS-002</code> (datasheets missing but MPNs set) and <code>DS-003</code> (partial MPN coverage) are softer variants with the same implication for the parts they cite.

## Design Review Contract

When the user asks for a **design review**, **complete report**, **ready-to-fab assessment**, or anything equivalent, do not stop at running one or two analyzers and summarizing their findings. A design review in this skill has a stricter contract:

1. Read the full workflow in this <code>SKILL.md</code>, not just the analyzer command sections.
2. Read <code>references/report-generation.md</code> before writing the report.
3. Run every applicable analyzer for the files present in the project, then say explicitly which ones were and were not run.
4. Perform raw-file and datasheet cross-verification before claiming anything is "verified".
5. Triage likely analyzer false positives before elevating them into blockers.
6. If a required step could not be done, state it as a review gap, not as silent omission.

Treat this as the minimum bar. Analyzer JSON alone is not the final review.

### Minimum Review Checklist

For a full design review, explicitly account for each item below in the report:

- <code>datasheets/</code> present, synced, or verification gap stated
- <code>analyze_schematic.py</code>
- <code>analyze_pcb.py --full</code>
- <code>cross_analysis.py</code>
- <code>analyze_emc.py</code>
- SPICE simulation when any simulator is installed
- <code>analyze_thermal.py</code> when both schematic and PCB JSON exist
- <code>analyze_gerbers.py</code> when fabrication outputs exist
- lifecycle audit when network access and MPN coverage allow it
- prior review / prior run delta check
- raw schematic/PCB spot-verification elevated to full verification for critical parts
- explicit report sections for blockers, verification basis, false positives, and skipped analyses

If an item is not applicable, say why. If it was skipped, say why. If it failed, say how that limits confidence.

### Common Review Failure Modes

These are the failure modes this contract is meant to prevent:

- Stopping after schematic + PCB + EMC output and calling it a complete review
- Reporting analyzer findings without checking whether they are expected layout artifacts
- Claiming "verified" without direct datasheet evidence or structured extraction evidence
- Omitting thermal, lifecycle, prior-review delta, or gerber checks without disclosure
- Writing a report that lacks a verdict, blockers table, verification basis, or skipped-analysis notes
- Reading only the first part of this skill and missing the design-review workflow later in the file

## PDF Schematic Analysis

This skill also handles **PDF schematics** — reference designs, dev board schematics, eval board docs, application notes, and datasheet typical-application circuits. Common use cases:

- Analyze a manufacturer's reference design to understand the circuit
- Extract a subcircuit (power supply, USB interface, sensor front-end) to incorporate into your own KiCad design
- Compare a PDF reference design against your own schematic
- Extract a full BOM from a PDF schematic
- Validate component values in a PDF against current datasheets

**Workflow:** Read the PDF pages visually → identify components and connections → extract structured data → translate to KiCad symbols and nets → validate against datasheets.

For the full methodology — component extraction, notation conventions, net mapping, subcircuit extraction, KiCad translation, and validation — read <code>references/pdf-schematic-extraction.md</code>.

For deep validation of extracted circuits against datasheets (verifying values, checking patterns, detecting errors), use the methodology in <code>references/schematic-analysis.md</code>.

## Analysis Scripts

This skill includes Python scripts that extract comprehensive structured JSON from KiCad files in a single pass. Run these first, then reason about the output.

Read analyzer JSON output directly rather than writing ad-hoc extraction scripts. The JSON schema has specific field names (documented below and in <code>references/output-schema.md</code>) that are easy to get wrong in custom code. To extract a specific section: <code>python3 -c "import json; d=json.load(open('file.json')); print(json.dumps(d['key'], indent=2))"</code>.

**When the JSON surprises you** — an AttributeError, unexpected shape, field
returning <code>None</code> that "should" have a value — stop and run <code>--schema</code> before
writing a second extraction attempt. It prints the exact field names and
types for every top-level key:

<pre><code>
python3 <skill-path>/scripts/analyze_schematic.py --schema
python3 <skill-path>/scripts/analyze_pcb.py --schema
python3 <skill-path>/scripts/analyze_gerbers.py --schema
</code></pre>

**JSON field cheat sheet** — the most common mistakes when reading analyzer
output by hand:

| What you want | Correct path and field | Common mistake |
|---------------|-----------------------|----------------|
| Pins on a net | <code>nets[<name>].pins[].component / .pin_number / .pin_name / .pin_type</code> | <code>ref</code>, <code>pin</code>, <code>type</code>, <code>number</code> |
| Unnamed-net pretty display | <code>nets[<name>].display_name</code> — when set, a <code>Ref.PinName</code> hint for an <code>__unnamed_N</code> net whose only named IC pin tells the story (e.g. <code>__unnamed_36 → U1.VBOOT</code>). Absent means the analyzer couldn't disambiguate. | Ignoring <code>display_name</code> and pasting raw <code>__unnamed_36</code> into the report |
| IC pin map | <code>ic_pin_analysis[]</code> is a **list** of IC entries; each has <code>.reference</code> and <code>.pins[]</code> with <code>.pin_number / .pin_name / .pin_type / .net / .connected_to[]</code>. Scope: <code>type</code> in <code>{ic, connector, crystal, oscillator}</code> only. | Treating it as <code>{ref: {...}}</code> or <code>pins[].number</code> |
| Transistor pin map | <code>transistor_pin_analysis[]</code> — **separate** list for <code>type=transistor</code> (MOSFETs, BJTs, FETs), same per-entry shape as <code>ic_pin_analysis[]</code>. Use this for half-bridge / gate-driver pin verification. | Looking inside <code>ic_pin_analysis[]</code> for <code>Q1</code> — transistors are not there |
| Detected circuits | Every pattern-matched circuit (power regulators, RC filters, crystal oscillators, bridges, …) lives in <code>findings[]</code> — filter with <code>finding_schema.get_findings(data, Det.POWER_REGULATORS)</code> etc. **Do not read from <code>subcircuits[]</code>**: that's an IC-neighborhood grouping (<code>{center_ic, ic_value, neighbor_components, …}</code>), not a categorized detection index | Looking for <code>subcircuits.power_regulators</code>, <code>subcircuits.rc_filters</code>, or any <code>subcircuits[type]</code> key — these never existed in v1.3 output |
| Zone net | <code>pcb.zones[].net</code> is an **integer net ID**, not a string. Use <code>f"{net!r}"</code> or convert first | <code>f"{net:20s}"</code> — crashes with <code>ValueError: Unknown format code 's' for object of type 'int'</code> |
| Zone layer | <code>pcb.zones[].layers</code> (plural) is the canonical layer list. <code>zones[].layer</code> (singular) is reserved/None on multi-layer zones — always read <code>.layers</code>. | Reading <code>zones[].layer</code> and getting <code>None</code> |
| Footprint position | <code>pcb.footprints[].x / .y</code> at top level (no <code>.position</code> wrapper) | <code>footprints[].position.x</code> |
| Per-pad net info on a footprint | <code>pcb.footprints[].pad_nets{pad_number: {net, pin}}</code> is a dict keyed by pad number. <code>connected_nets[]</code> gives the deduped list of nets touching the footprint. | <code>footprints[].pads[]</code> — that key does not exist in the output |
| Tracks summary | <code>pcb.tracks</code> is a **dict** (the <code>Tracks</code> envelope): <code>{segment_count, arc_count, layer_distribution{}, width_distribution{}}</code>. Only <code>--full</code> populates the inner <code>tracks.segments[]</code> and <code>tracks.arcs[]</code> arrays. Segment fields: <code>{x1, y1, x2, y2, width, layer, net}</code> — <code>net</code> is an **int** id (map via top-level <code>nets</code> / <code>net_name_to_id</code>). | <code>for t in tracks: ...</code> without <code>--full</code> — <code>tracks</code> is the summary dict, not a list; <code>seg.get("x")</code> / <code>seg.get("start")</code> — wrong keys, and <code>.get()</code> defaults turn them into silent-0.0 bugs |
| Power net routing | <code>pcb.power_net_routing</code> is a **list** of per-net entries <code>[{net, track_count, total_length_mm, ...}, ...]</code>, not a dict keyed by net. | <code>power_net_routing["VCC"]</code> → TypeError |
| Findings | <code>findings[]</code> flat list — each has <code>rule_id</code>, <code>detector</code>, <code>severity</code>, <code>summary</code>, <code>report_context</code>. Filter with <code>finding_schema.get_findings(data, Det.*)</code> or <code>group_findings(data)</code> | Looking for keyed dicts like <code>signal_analysis.power_regulators[]</code> (pre-v1.3 format, removed) |

This prevents format-string bugs and wrong field names. Use f-strings or <code>json.dumps()</code> for output formatting — never <code>%s</code> with non-string types. See <code>references/output-schema.md</code> for the full schema with common extraction patterns.

In all commands below, <code><skill-path></code> refers to this skill's base directory (shown at the top of this file when loaded).

### Schematic Analyzer
<pre><code>
python3 <skill-path>/scripts/analyze_schematic.py <file.kicad_sch> --analysis-dir analysis/
python3 <skill-path>/scripts/analyze_schematic.py <file.kicad_sch> --analysis-dir analysis/ --compact
python3 <skill-path>/scripts/analyze_schematic.py <file.kicad_sch> --output analysis.json  # one-off, no cache
</code></pre>
Outputs structured JSON (~60-220KB depending on board complexity) with:
- **Components & BOM**: inventory with reference, value, footprint, lib_id, type classification, MPN, datasheet; deduplicated BOM with quantities
- **Nets**: full connectivity map with pin-to-net mapping, wire counts, no-connects
- **Detected subcircuits** (pattern-matched circuits — all emitted as <code>findings[]</code> entries with matching <code>Det.*</code> detectors; use <code>get_findings(data, Det.POWER_REGULATORS)</code> etc. to fetch):
  - Power regulators — LDO/switching/inverting topology, Vout estimation via datasheet-verified Vref lookup (~60 families) with heuristic fallback and fixed-output suffix parsing, <code>vref_source</code> (<code>lookup</code>/<code>heuristic</code>/<code>fixed_suffix</code>) and <code>vout_net_mismatch</code> fields
  - Voltage dividers, RC/LC filters (cutoff frequency), feedback networks, crystal circuits (load cap analysis, IC pin-based detection)
  - Op-amp circuits (configuration, gain, integrator/compensator), transistor circuits (net-name-aware load classification: motor/heater/fan/solenoid/valve/pump/relay/speaker/buzzer/lamp; FET level shifter topology)
  - Bridge circuits (H-bridge, 3-phase, cross-sheet detection), protection devices (ESD/TVS), current sense, decoupling analysis
  - Domain-specific: RF chains, RF matching networks, BMS, Ethernet (BFS PHY-to-connector tracing), HDMI/DVI interfaces, memory interfaces, key matrices (net-name and topology-based), isolation barriers, addressable LED chains (WS2812/SK6812/APA102), battery chargers (TP4056/MCP73831/BQ2404x), motor drivers (A4988/TMC2209/DRV8301), ESD protection coverage audit, debug interfaces (SWD/JTAG with MCU tracing), power path (load switches/ideal diodes/USB PD controllers), ADC signal conditioning (external ADCs + voltage references with anti-aliasing cross-ref), reset/supervisor circuits (voltage supervisors/watchdogs/RC reset networks), clock distribution (clock generators/PLLs/oscillator output tracing), display/touch interfaces (SSD1306/ILI9341/ST7789/FT6236/GT911), sensor fusion (IMU/environmental/magnetometer with interrupt validation and bus clustering), level shifters (IC-based + discrete BSS138 with supply domain mapping), audio circuits (amplifiers/codecs with I2S/class-D detection), LED driver ICs (PWM/matrix/constant-current), RTC circuits (battery backup/crystal pairing), LED lighting audit (current limiting validation), thermocouple/RTD interfaces (MAX31855/MAX31865), power sequencing validation (power tree/enable chain/PG daisy chain analysis)
- **IC pinout analysis**: pin-level connectivity, IC function classification (3-tier: library prefix, part number keywords, description fallback)
- **Power analysis**: PDN impedance (1kHz–1GHz with MLCC parasitics), power budget, power sequencing (EN/PG chains), sleep current audit (resistive paths + regulator Iq with EN detection), voltage derating, inrush estimation
- **Design analysis**: ERC warnings, power domains, bus detection (I2C/SPI/UART/CAN/RS-485 with COPI/CIPO/SDI/SDO), differential pairs (suffix-pair matching for USB/LVDS/Ethernet/HDMI/MIPI/PCIe/SATA/CAN/RS-485), cross-domain signals (voltage equivalence), BOM optimization, test coverage, assembly complexity, USB compliance
- **Quality checks**: annotation completeness, label validation, PWR_FLAG audit, footprint filter validation, sourcing audit, property pattern audit, generic transistor symbol detection (flags Q_NPN_*/Q_PNP_*/Q_NMOS_*/Q_PMOS_* symbols with datasheet availability check)
- **Structural**: MCU alternate pin summary, ground domain classification, bus topology, wire geometry, spatial clustering, pin coverage, hierarchical label validation

Supports modern <code>.kicad_sch</code> (KiCad 6+) and legacy <code>.sch</code> (KiCad 4/5). Hierarchical designs parsed recursively.

**Legacy format:** For KiCad 5 legacy <code>.sch</code> files, the analyzer parses <code>.lib</code> files (cache libraries and project libs) to populate pin data. Pin-to-net mapping, signal analysis, and subcircuit detection all work when <code>.lib</code> files are available. Coverage is typically 92–100% — components whose <code>.lib</code> files are missing (standard KiCad system libs not in the repo) will lack pin data. Built-in fallbacks cover 40+ common symbols (R, C, L, D, LED, transistors, MOSFETs, crystals, switches, polarized caps, connectors up to 20-pin, resistor packs) with mil-based pin offsets and automatic wire-snap correction for version-mismatched pin positions.

### Supplementary Data for Legacy Designs

When <code>analyze_schematic.py</code> returns incomplete data (components with missing pins due to unavailable <code>.lib</code> files), use additional project files to recover full analysis capability. The most valuable source is the <code>.net</code> netlist file, which provides explicit pin-to-net mapping that closes any remaining gaps.

For detailed parsing instructions, data recovery workflows, and a priority matrix of supplementary sources (netlist, cache library, PCB cross-reference, PDF exports), read <code>references/supplementary-data-sources.md</code>.

**Verify analyzer output against reality.** The analyzer can silently produce plausible-looking but incorrect results — wrong voltage estimates, missing MPNs, wrong pin-to-net mappings. These don't cause script errors; they just produce bad data that flows into your report. In testing across multiple boards, every project had at least one misleading analyzer output. Cross-reference against the raw <code>.kicad_sch</code> file:

1. **Component count** — grep for <code>(symbol (lib_id</code> blocks, subtract power symbols. Must match analyzer count exactly.
2. **Pin-to-net mapping** — verify the analyzer's pin-to-net mapping against the raw schematic for each component. Read the symbol block, trace wires/labels to confirm connections. Cross-reference IC pin assignments against the manufacturer's datasheet pin table. This is the highest-value verification step — a wrong pin mapping produces a non-functional board and is invisible to DRC/ERC.
3. **Physical correctness (not just consistency)** — consistency checks (schematic=PCB=analyzer all agree) are necessary but not sufficient. They only confirm the design is internally coherent — not that it matches the real-world part. The most dangerous case: a transistor symbol encodes a pinout assumption (like <code>Q_NPN_BEC</code> = pin 1=B, 2=E, 3=C) that doesn't match the actual part. Everything passes consistency checks, but the board is wrong. To catch this:
   - For transistors (BJT/MOSFET) in SOT-23, SOT-223, TO-252 and similar packages, the KiCad <code>lib_id</code> suffix encodes a pin ordering assumption. SOT-23 BJTs exist in at least 6 pinout variants (BEC, BCE, EBC, ECB, CBE, CEB); SOT-23 MOSFETs in GDS, GSD, SGD, DSG. If no MPN is specified, there's no way to verify the assumption — flag this as a critical ambiguity.
   - When an MPN is specified, verify the symbol's pin-to-pad assignment against the datasheet's pinout diagram for that specific package.
   - This principle extends beyond transistors — any component where multiple pin orderings exist for the same package (voltage regulators with different pin assignments, connectors with vendor-specific pinouts) needs MPN-level verification.
   - **When verification isn't possible, assess plausibility.** Not all unverified choices carry equal risk. Some align with strong conventions (the most common SOT-23 NPN pinout is BCE; 2N2222 in SOT-23 is almost always BCE); others go against convention or are genuinely ambiguous (SOT-23 MOSFETs have no dominant standard). When an MPN is missing and you can't verify, use domain knowledge — typical pinouts for that device type and package, manufacturer conventions, what the majority of parts in that category do — to assess whether the assumed pinout is likely correct, unusual, or a coin flip. Report the confidence level: "matches the most common convention" is different from "could go either way." This same reasoning applies to passive values (is 4.7kΩ a typical pull-up value for this bus?), circuit topologies (is this a standard application circuit?), and component selection (is this part commonly used for this purpose?).
4. **Net trace** — trace power rails and critical signal nets end-to-end through wires/labels. Verify the analyzer's pin list is complete for each net.
5. **Regulator Vout** — check the <code>vref_source</code> field. <code>"lookup"</code> means datasheet-verified (~60 families); <code>"heuristic"</code> means it's a guess that needs manual verification. The <code>vout_net_mismatch</code> field flags estimated Vout differing >15% from the output rail name voltage.
6. **Hierarchical connectivity** — on multi-sheet designs, verify sub-sheet connections are reflected in the net data.

See <code>references/schematic-analysis.md</code> Step 2 for the full verification checklist. If the script fails or returns unexpected results, see <code>references/manual-schematic-parsing.md</code> for the complete fallback methodology.

### PCB Layout Analyzer
<pre><code>
python3 <skill-path>/scripts/analyze_pcb.py <file.kicad_pcb> --analysis-dir analysis/
python3 <skill-path>/scripts/analyze_pcb.py <file.kicad_pcb> --analysis-dir analysis/ --proximity  # add crosstalk analysis
python3 <skill-path>/scripts/analyze_pcb.py <file.kicad_pcb> --output pcb.json  # one-off, no cache
</code></pre>
Outputs structured JSON (~50-300KB depending on board complexity) with:
- **Core**: footprint inventory (pads, courtyards, net assignments, extended attrs, schematic cross-reference), track/via statistics, zone summaries, board outline/dimensions, routing completeness
- **Zones & copper presence**: zone outline vs filled polygon bounding boxes, fill ratio, cross-layer copper presence at every pad (which components have zone copper on the opposite layer and which don't), same-layer foreign zone detection
- **Via analysis**: type breakdown (through/blind/micro), annular ring checks, via-in-pad detection, BGA/QFN fanout patterns, current capacity, stitching via identification, tenting
- **Signal integrity**: per-net trace length, layer transition tracking (ground return paths), trace proximity/crosstalk (with <code>--proximity</code>)
- **Power & thermal**: current capacity per net, power net routing summary, ground domain identification (AGND/DGND), zone stitching via density, thermal pad detection and via counting
- **Manufacturing**: placement analysis (courtyard overlaps, edge clearance), decoupling cap distances, DFM scoring (JLCPCB standard/advanced tier), tombstoning risk (0201/0402 thermal asymmetry), thermal pad via adequacy, silkscreen documentation audit

Add <code>--full</code> to include individual track/via coordinates, per-segment trace impedance (microstrip Z0 from stackup), pad-to-pad routed distances, return path continuity analysis, and via stub lengths. The <code>--full</code> output feeds the <code>spice</code> skill's parasitic extraction (<code>extract_parasitics.py</code>) for PCB-aware simulation. Supports KiCad 5 legacy format.

**Zone fills must be current.** The copper presence analysis uses KiCad's filled polygon data, which is computed when the user runs Edit → Fill All Zones (shortcut <code>B</code>) and stored in the <code>.kicad_pcb</code> file. If the board was modified after the last fill, the filled polygon data may be stale and the copper presence results will be inaccurate. When reviewing copper presence data, note whether the <code>fill_ratio</code> seems reasonable — a zone with 0 filled area or <code>is_filled: false</code> likely hasn't been filled.

**Zone outline ≠ actual copper.** The zone <code>outline_bbox</code> is the user-drawn boundary; <code>filled_bbox</code> is where copper actually exists after clearances, keepouts, and priority cuts. The <code>copper_presence</code> section shows which components have zone copper on the opposite layer — use this for capacitive touch pad isolation, antenna keep-out, and thermal analysis instead of inferring copper presence from zone outlines.

**Copper-sensitive components need deeper checks.** For capacitive touch pads and antennas, confirming "no opposite-layer copper" is necessary but not sufficient. The copper absence could be accidental — one zone refill after a routing change could add copper and kill touch sensitivity or detune the antenna. Check for explicit **keepout zones** (rule areas) that enforce the copper-free area as a DRC rule. Also measure same-layer GND clearance around touch pads and compare against the controller's app note minimum. For touch pads, compare trace lengths across all pads — significant asymmetry shifts baseline readings per channel. Report physical details (pad size, position, clearance, trace width/length) for all copper-sensitive components. See <code>references/pcb-layout-analysis.md</code> → Copper-Sensitive Components for the full checklist.

**Verify after every run:** Confirm footprint count and board outline dimensions against the raw <code>.kicad_pcb</code> file. Verify pad-to-net assignments for IC footprints against the schematic's pin-to-net mapping — this catches library footprint errors where pad numbering doesn't match the symbol pinout. If the script fails, see <code>references/manual-pcb-parsing.md</code> for the fallback methodology.

### PCB Rich Format and Assembly Checks

All PCB analysis sections now produce findings with the rich format (detector, rule_id, category, severity, confidence, summary, recommendation, report_context). Additionally, 7 new assembly/DFM checks run automatically:

- **FD-001**: Fiducial marker presence (>= 3 per SMD side)
- **TE-001**: Test point coverage across signal nets
- **OR-001**: Passive component orientation consistency
- **SK-001**: Silkscreen text overlapping exposed pads
- **VP-001**: Via-in-pad without tenting (--full mode)
- **BV-001**: Via clearance from board edges (--full mode)
- **KO-001**: Keepout zone violations
- **CP-001**: Same-layer foreign zone under a component. Severity is <code>warning</code> when the foreign zone is a non-ground net or the component has no GND pad; severity is <code>info</code> when the foreign zone is GND and the component has a GND pad (the common case of a bypass cap sitting over the ground pour — expected layout, not a clearance issue).

### Cross-Domain Analysis

After running both schematic and PCB analyzers, run the cross-domain analyzer.
Point <code>--schematic</code> and <code>--pcb</code> at the current run's JSON files and pass
<code>--analysis-dir analysis/</code> so the result lands inside the same run folder
and the manifest tracks it:

<pre><code>
# Recommended: integrate into the current run
python3 <skill-path>/scripts/cross_analysis.py \
    --schematic analysis/<run_id>/schematic.json \
    --pcb analysis/<run_id>/pcb.json \
    --analysis-dir analysis/

# One-off (bypasses the cache)
python3 <skill-path>/scripts/cross_analysis.py \
    --schematic schematic.json --pcb pcb.json --output cross_analysis.json
</code></pre>

Checks: CC-001 connector current capacity, EG-001 ESD protection gaps, DA-001 decoupling adequacy, XV-001..003 schematic/PCB sync. PCB JSON optional.

Mechanical cross-verify (PCB vs schematic geometry): <code>python3 <skill-path>/scripts/cross_verify.py --help</code>.

### Connectivity Graph (--full mode)

When <code>--full</code> is used with the PCB analyzer, the output includes a <code>connectivity_graph</code> section with per-net copper connectivity analysis via union-find over pads, tracks, vias, and zone fills. This enables deterministic plane split detection and return path validation in cross_analysis.py. **The top-level keys of <code>connectivity_graph</code> are net names themselves** — e.g., <code>cg['GND']</code>, <code>cg['Net-(D2-K)']</code>, <code>cg['+3V3']</code> — *not* a <code>per_net</code> wrapper. Each net entry shows island count, component-to-island mapping (<code>{component:pad: island_id}</code>), gap locations, and disconnected pad pairs.

### Gerber & Drill Analyzer
<pre><code>
# Recommended: integrate into the current run
python3 <skill-path>/scripts/analyze_gerbers.py <gerber_directory/> --analysis-dir analysis/

# One-off
python3 <skill-path>/scripts/analyze_gerbers.py <gerber_directory/> --output gerber.json
</code></pre>
Outputs: layer identification (X2 attributes), component/net/pin mapping (KiCad 6+ TO attributes), aperture function classification, trace width distribution, board dimensions, drill classification (via/component/mounting), layer completeness, alignment verification, pad type summary (SMD/THT ratio). Add <code>--full</code> for complete pin-to-net connectivity dump. ~10KB JSON.

The gerber analyzer produces a <code>findings</code> list with rich format findings: GR-001 missing layers, GR-002 alignment issues, GR-003 drill problems, GR-004 paste aperture mismatches, GR-005 open board outlines.

If the script fails or returns unexpected results, see <code>references/manual-gerber-parsing.md</code> for the complete fallback methodology for parsing raw Gerber/Excellon files directly.

All scripts output JSON to stdout by default. Prefer <code>--analysis-dir analysis/</code>
to integrate output into the run-folder convention described in "Analysis
Cache Convention" below — every analyzer in a single session then co-locates
inside the same <code>analysis/<run_id>/</code> folder and is tracked by the manifest.
Use <code>--output file.json</code> only for one-off runs where you don't want the
result cached. Add <code>--compact</code> for single-line JSON.

**Analyzer JSON is worth keeping** — these are expensive to regenerate (large
schematics take time). <code>--analysis-dir</code> preserves every run and is the form
downstream tools (diff_analysis, what_if) expect. They're not worth
committing to git, but don't delete them between analysis steps.

### Harmonized Output Format

All analyzers produce a uniform output envelope:

<pre><code>
{
    "analyzer_type": "schematic|pcb|emc|cross_analysis|thermal|gerber|lifecycle|spice",
    "schema_version": "1.4.0",
    "summary": {
        "total_findings": 42,
        "by_severity": {"error": 3, "warning": 15, "info": 24}
    },
    "findings": [
        {"rule_id": "...", "detector": "...", "severity": "...", "confidence": "...", "evidence_source": "...", "summary": "...", ...}
    ],
    "trust_summary": {
        "total_findings": 42,
        "trust_level": "high|mixed|low",
        "by_confidence": {"deterministic": 20, "heuristic": 18, "datasheet_backed": 4},
        "by_evidence_source": {"datasheet": 4, "topology": 10, "heuristic_rule": 18, ...},
        "provenance_coverage_pct": 96.5
    }
}
</code></pre>

The <code>findings</code> list is the single authoritative source for all findings. Use <code>finding_schema.get_findings()</code> or <code>finding_schema.group_findings()</code> to filter by detector, rule prefix, or category. Detector names are available as constants in <code>finding_schema.Det</code>. Severities are <code>error</code>, <code>warning</code>, or <code>info</code>; confidence is <code>deterministic</code>, <code>heuristic</code>, or <code>datasheet-backed</code>.

All analyzers support <code>--text</code> for human-readable output, <code>--analysis-dir</code> for
integrated run-folder output (preferred), and <code>--output</code> for writing to a
specific file verbatim (one-off). When both are passed, the explicit
<code>--output</code> path wins — pick one form per invocation.

### Stage and Audience Filtering

All analyzers support <code>--stage</code> and <code>--audience</code> flags:

**Stages:** <code>schematic</code>, <code>layout</code>, <code>pre_fab</code>, <code>bring_up</code>
**Audiences:** <code>designer</code> (default), <code>reviewer</code>, <code>manager</code>

<pre><code>
# Show only layout-relevant findings for a reviewer
python3 <skill-path>/scripts/analyze_pcb.py board.kicad_pcb --stage layout --audience reviewer --text

# Manager summary of schematic review readiness
python3 <skill-path>/scripts/analyze_schematic.py design.kicad_sch --audience manager --text

# Pre-fab checklist for cross-domain analysis
python3 <skill-path>/scripts/cross_analysis.py -s sch.json -p pcb.json --stage pre_fab --text
</code></pre>

JSON output always includes all findings. <code>--stage</code> adds <code>stages</code> and <code>in_active_stage</code> fields to each finding plus a <code>stage_filter</code> summary. <code>audience_summary</code> is always computed with designer/reviewer/manager views. <code>--text</code> output respects both flags.

### Generated Files

Analysis outputs are stored in <code>analysis/</code> with timestamped run folders managed by <code>analysis_cache.py</code>. The manifest (<code>analysis/manifest.json</code>) tracks all runs.

| File Type | Location | Regenerable? | Commit to git? |
|-----------|----------|-------------|----------------|
| Analyzer JSON | <code>analysis/<timestamp>/*.json</code> | Yes (expensive) | Configured by <code>track_in_git</code> in <code>.kicad-happy.json</code> (default: no) |
| Manifest | <code>analysis/manifest.json</code> | Yes | Always (tracked by default) |
| Design review report | User-chosen path | Yes | Optional |

When creating design reviews, check the manifest for prior runs. If <code>auto_diff</code> is enabled and prior runs exist, automatically diff current vs previous using <code>diff_analysis.py</code> and include the delta in the "Previous Review Delta" section.

See also the <code>bom</code> skill's cleanup section for datasheets, order CSVs, and backups.

### Analysis Cache Configuration

The <code>analysis</code> section in <code>.kicad-happy.json</code> controls the shared analysis output directory:

<pre><code>
{
  "analysis": {
    "output_dir": "analysis",
    "retention": 5,
    "auto_diff": true,
    "track_in_git": false,
    "diff_threshold": "major"
  }
}
</code></pre>

| Field | Default | Description |
|-------|---------|-------------|
| <code>output_dir</code> | <code>"analysis"</code> | Analysis directory path, relative to project root |
| <code>retention</code> | <code>5</code> | Max unpinned runs to keep. <code>0</code> = unlimited |
| <code>auto_diff</code> | <code>true</code> | Auto-include delta section in design reviews |
| <code>track_in_git</code> | <code>false</code> | When false, JSONs gitignored but manifest tracked |
| <code>diff_threshold</code> | <code>"major"</code> | Severity that triggers new timestamped folder: <code>minor</code>, <code>major</code>, <code>breaking</code> |

All fields are optional. Missing fields use defaults.

### Output JSON Schema Quick Reference

**Schematic analyzer top-level keys:**
<pre><code>
analyzer_type, schema_version, summary, findings, trust_summary,
file, kicad_version, file_version, title_block, statistics,
bom, components, nets, subcircuits, ic_pin_analysis, transistor_pin_analysis,
design_analysis, connectivity_issues, hierarchy_context, hierarchy_warning,
net_classifications, rail_voltages
</code></pre>
Optional (present when non-empty): <code>pdn_impedance</code>, <code>sleep_current_audit</code>, <code>power_budget</code>, <code>power_sequencing</code>, <code>bom_optimization</code>, <code>test_coverage</code>, <code>assembly_complexity</code>, <code>usb_compliance</code>, <code>inrush_analysis</code>, <code>sheets</code> (multi-sheet only), <code>missing_info</code>, <code>bom_lock</code>, <code>project_settings</code>

Key nested structures:
- <code>statistics</code>: <code>{total_components, unique_parts, dnp_parts, total_nets, total_wires, total_no_connects, component_types, power_rails, missing_mpn, ...}</code>
- <code>bom[]</code>: <code>{reference, references[], value, footprint, mpn, manufacturer, datasheet, quantity, dnp, ...}</code>
- <code>components[]</code>: <code>{reference, value, footprint, lib_id, lib_name, type, category, mpn, datasheet, dnp, in_bom, parsed_value, ...}</code>
- <code>nets{net_name}</code>: <code>{pins[], wires, labels[], ...}</code> — each pin: <code>{component, pin_number, pin_name, pin_type, ...}</code> (NOT <code>ref</code> or <code>pin</code>)
- <code>subcircuits[]</code>: IC-neighborhood groupings (<code>{center_ic, ic_value, neighbor_components, ...}</code>), NOT a categorized detection index — see the JSON field cheat sheet at the top of this file.
- **Detected subcircuits live in <code>findings[]</code>** — power regulators, voltage dividers, RC/LC filters, feedback networks, opamp/transistor/bridge/crystal circuits, current sense, decoupling, protection, buzzer/speaker, Ethernet/HDMI/memory interfaces, RF chains/matching, BMS, key matrices, isolation barriers, addressable LED chains, and design observations all emit as findings with matching <code>Det.*</code> detectors. Use <code>get_findings(data, Det.POWER_REGULATORS)</code> etc. to fetch them. The pre-v1.3 <code>signal_analysis</code> wrapper and its top-level detection lists are gone.

**PCB analyzer top-level keys:**
<pre><code>
analyzer_type, schema_version, summary, findings, trust_summary,
file, kicad_version, file_version, statistics, layers, setup,
nets, net_name_to_id, board_outline, component_groups, footprints,
tracks, vias, zones, keepout_zones, connectivity, net_lengths
</code></pre>
Optional: <code>power_net_routing</code>, <code>decoupling_placement</code>, <code>ground_domains</code>, <code>layer_transitions</code>, <code>silkscreen</code>, <code>board_metadata</code>, <code>dimensions</code>, <code>groups</code>, <code>net_classes</code>, <code>dfm_summary</code>, <code>placement_density</code>, <code>copper_presence_summary</code>, <code>board_thickness_mm</code>, <code>trace_proximity</code> (with <code>--proximity</code>). Sections previously at top level (<code>thermal_analysis</code>, <code>thermal_pad_vias</code>, <code>tombstoning_risk</code>, <code>placement_analysis</code>, <code>current_capacity</code>, <code>copper_presence</code>, <code>dfm</code>) are now in <code>findings[]</code>. With <code>--full</code>, the output also includes a <code>connectivity_graph</code> section (see "Connectivity Graph" above).

Key nested structures:
- <code>net_lengths</code> is a **list** (not dict): <code>[{net, net_number, total_length_mm, segment_count, via_count, layers{}}, ...]</code> sorted by length descending
- <code>power_net_routing</code> is a **list**: <code>[{net, track_count, total_length_mm, min_width_mm, max_width_mm, widths_used[]}, ...]</code>
- <code>footprints[]</code>: <code>{reference, value, footprint, layer, pads[], sch_path, sch_sheetname, sch_sheetfile, connected_nets[], ...}</code>
- <code>statistics</code>: <code>{footprint_count, copper_layers_used, smd_count, tht_count, zone_count, via_count, routing_complete, ...}</code>

**Gerber analyzer top-level keys:**
<pre><code>
analyzer_type, schema_version, summary, findings, trust_summary,
directory, generator, layer_count, statistics, completeness, alignment,
drill_classification, pad_summary, board_dimensions, gerbers, drills
</code></pre>

**Workflow:** When analyzing a KiCad project, scan the project directory for all available file types and run **every applicable analyzer** — not just the one the user mentioned. A complete analysis uses all the data available. Use <code>--analysis-dir analysis/</code> on all analyzers to share a single run folder tracked by the manifest. For one-off runs without cache tracking, use <code>--output file.json</code> instead.

**Before starting the workflow below for a design review:** read <code>references/report-generation.md</code>. The report structure, verification basis rules, skipped-analysis disclosure, and false-positive triage expectations there are part of the review workflow, not optional polish added at the end.

1. **Scan the project directory** for <code>.kicad_sch</code>, <code>.kicad_pcb</code>, <code>.kicad_pro</code>, gerber directories, and <code>.net</code>/<code>.xml</code> netlist files.
2. **Sync datasheets** (see Datasheet Acquisition below) — this is a prerequisite for verification, not optional. Without datasheets, all subsequent verification is reduced to internal consistency checks — confirming the design agrees with itself, not that it's correct. Run the sync before reading any analyzer output. If sync fails or no API keys are available, use fallback methods (Datasheet property URLs, individual downloads via <code>digikey</code> skill, ask the user). If critical IC datasheets can't be obtained, note this prominently in the report as a verification gap.
3. **Run the core analyzers.** If the schematic exists, run <code>analyze_schematic.py</code>. If the PCB exists, run <code>analyze_pcb.py --full</code>. If gerbers exist, run <code>analyze_gerbers.py</code>. Run them in parallel when possible.
4. **Run cross-domain analysis** — when both schematic and PCB analysis exist, run <code>cross_analysis.py --schematic sch.json --pcb pcb.json</code>. This catches dangerous cross-domain bugs (connector current vs trace width, ESD gaps, decoupling adequacy, schematic/PCB sync).
5. **Run EMC pre-compliance** — when both schematic and PCB analysis exist, run <code>python3 <emc-skill-path>/scripts/analyze_emc.py --schematic sch.json --pcb pcb.json</code> (the script lives in the <code>emc</code> skill — not under this skill's <code><skill-path></code>). This is **required** during design reviews, not optional. The EMC skill runs 44 rule checks covering ground plane integrity, decoupling, switching harmonics, PDN impedance, diff pair skew, ESD paths, and more. Include results in the EMC section of the report.
6. **Run SPICE simulation** — first run <code>which ngspice ltspice xyce</code>. If any simulator is installed, SPICE is **required** before writing the report. Hand off to the <code>spice</code> skill: <code>python3 <spice-skill-path>/scripts/simulate_subcircuits.py sch.json</code>. This validates filter frequencies, divider ratios, opamp gains, and more against actual simulation results. SPICE takes <1 second on most boards and catches value-computation errors (wrong resistor ratio, wrong cap for cutoff frequency) that no static analyzer finds. If both schematic and PCB analysis exist, run <code>python3 <spice-skill-path>/scripts/extract_parasitics.py pcb.json --output parasitics.json</code> (needs the <code>--full</code> PCB JSON) and pass <code>--parasitics parasitics.json</code> — a path argument, not a bare flag — for high-impedance circuits (>100K feedback dividers, LC filters, RF matching networks). Include results in the Simulation Verification section of the report. **Output schema:** top-level keys are <code>summary</code>, <code>simulation_results</code>, <code>workdir</code>, <code>total_elapsed_s</code>, <code>simulator</code>. Each entry in <code>simulation_results[]</code> has: <code>subcircuit_type</code>, <code>components</code> (list of refs, e.g. <code>["R5", "C3"]</code>), <code>reference</code> (joined refs, e.g. <code>"R5/C3"</code>), <code>status</code> (<code>pass</code>/<code>warn</code>/<code>fail</code>/<code>skip</code>), <code>expected</code> (dict of metric values), <code>simulated</code> (dict of measured values), <code>delta</code> (dict of error percentages).
7. **Run thermal analysis** — when both schematic and PCB analysis exist, run <code>analyze_thermal.py --schematic schematic.json --pcb pcb.json</code>. Estimates junction temperatures from package θJA and board thermal via correction. Include results in the Thermal Hotspot section of the report.
8. **Run lifecycle audit** (when network access and MPNs are available) — invoke via <code>analyze_schematic.py --lifecycle</code> flag. Checks component obsolescence status via distributor APIs. Include results in the Component Lifecycle section of the report, or note "Lifecycle audit not performed — [reason: no API keys / no network / no MPNs]."
9. **Read the <code>.kicad_pro</code>** project file directly (it's JSON) for design rules, net classes, and DRC/ERC settings.
10. **Check for prior design reviews** — scan the project directory for existing review files (<code>*review*.md</code>, <code>*design-review*.md</code>). If found, read the most recent one. If <code>auto_diff</code> is enabled and prior runs exist, run <code>diff_analysis.py</code> on current vs previous run and include the delta in the "Previous Review Delta" section.
11. **Verify each output** against the raw files and datasheets before using the data in your report.
12. **Produce a unified report** covering schematic analysis, PCB layout analysis, cross-domain findings, EMC risk assessment, simulation verification, thermal hotspots, and cross-reference findings. See <code>references/report-generation.md</code> for the report template.
13. **Disclose all review gaps explicitly** — if thermal, lifecycle, gerber, datasheet extraction, or prior-review delta were not performed, add a short "Not performed / limits" section to the report instead of omitting them silently.

The more data sources you combine, the more confident the analysis. A schematic-only review misses layout issues; a PCB-only review misses design intent. Always use everything available.

### Analysis Depth

Default to thorough analysis unless the user asks for a quick review. The reason: the bugs that kill boards are the ones that look correct at a glance. A spot-check might confirm 5 ICs are correct while the 6th has pins 3 and 4 swapped — and that's the one that kills the board. Thoroughness principles:

- **Verify all components, not a sample.** Pin-to-net errors on "simple" parts (reversed diode, wrong resistor in a divider, connector with wrong pin ordering) are just as fatal as swapped IC pins. Cover the full design.
- **Use datasheets as ground truth — not KiCad library symbols.** The analyzer, raw schematic, and KiCad library files all tell you what the design *says* — only the manufacturer's PDF datasheet tells you what it *should* say. A library symbol with a wrong pin mapping is the most dangerous class of bug precisely because everything is internally consistent: schematic, PCB, and analyzer all agree, but the board doesn't work. Verifying a pin assignment against the <code>.kicad_sym</code> file is circular — it's the source of the potential error. Download datasheets before starting verification (see "Datasheet Acquisition" below), open the actual PDF for each IC, extract the pin function table, and cite page/section numbers when reporting verification results.
- **Assess plausibility, not just verifiability.** When something can't be verified (missing MPN, missing datasheet), don't stop at "unverified." Use domain knowledge to assess whether the design choice aligns with common conventions or looks unusual. A 10kΩ I2C pull-up is unremarkable; a 100Ω I2C pull-up warrants a closer look even without a datasheet to check against. An SOT-23 NPN with BCE pinout matches the most common convention; one with CEB is unusual enough to flag. The goal is to distinguish "unverified but probably fine" from "unverified and suspicious." This applies to pinouts, passive values, circuit topologies, and component selection.
- **Think beyond what the analyzer detects.** The analyzer only finds patterns it's programmed for. When a section has no automated data, consider whether that's because the design doesn't need it (fine — say so briefly) or because the analyzer can't detect it (reason about it manually). Not every section needs a paragraph — "Not applicable: battery-powered, no mains input" is sufficient. But don't let empty data create blind spots in areas that matter for the specific design.

### Datasheet Acquisition

Before the Deep Review pass, check availability: the extraction cache
(<code>datasheets/extracted/</code>) is the fast path; PDFs on disk
(<code>datasheets/</code>) are the fallback — read them directly. If parts have
neither, offer to sync datasheets (digikey / lcsc / mouser /
element14 skills) before reviewing without them. Extractions carry a
quality flag — low-quality facts come back flagged, not hidden;
decide whether to trust them or re-read the PDF.

Datasheets are what separate a consistency check from a correctness check. Without them, you can confirm the design agrees with itself — but not that it matches the real-world parts. Obtain datasheets early in the workflow.

**Automated sync (preferred):** Run datasheet sync scripts early in the workflow. They download datasheets for all components with MPNs into a shared <code>datasheets/</code> directory with an <code>manifest.json</code> manifest. Run the preferred source first; if some parts fail, try others — they share the same directory and skip already-downloaded files.

<pre><code>
python3 <digikey-skill-path>/scripts/sync_datasheets_digikey.py <file.kicad_sch>
python3 <lcsc-skill-path>/scripts/sync_datasheets_lcsc.py <file.kicad_sch>
python3 <element14-skill-path>/scripts/sync_datasheets_element14.py <file.kicad_sch>
python3 <mouser-skill-path>/scripts/sync_datasheets_mouser.py <file.kicad_sch>
</code></pre>

DigiKey is best (direct PDF URLs). element14 is reliable (no bot protection). LCSC works for LCSC-only parts. Mouser is a last resort (often blocks downloads).

**Check for existing datasheets:** Before downloading, look for:
- <code><project>/datasheets/</code> with <code>manifest.json</code> (from a previous sync)
- <code><project>/docs/</code> or <code><project>/documentation/</code>
- PDF files in the project directory whose names contain MPNs
- <code>Datasheet</code> property URLs embedded in the KiCad symbols

**Fallback methods when automated sync isn't available or misses parts:**
1. Use the <code>Datasheet</code> property URL from the schematic symbol — many KiCad libraries include direct PDF links
2. Use the <code>digikey</code> skill to search by MPN and download individual datasheets
3. Use web search to find the manufacturer's datasheet page
4. **Ask the user** — if a critical component's datasheet can't be found automatically, tell the user which parts are missing and ask them to provide the datasheets. Don't silently skip verification because a datasheet wasn't available. Example: "I couldn't find datasheets for U3 (XYZ1234) and U7 (ABC5678). Can you provide them? I need them to verify the pinout and application circuit."

**Structured datasheet extraction (for large designs or repeated reviews):** Pre-extract datasheet specs into cached JSON for faster, more consistent pin verification. This is especially valuable for designs with 10+ ICs where re-reading PDFs from scratch each time is slow.

<pre><code>
python3 <skill-path>/scripts/datasheet_page_selector.py <pdf_path> --mpn <mpn> --category <category>
</code></pre>

After reading the selected pages and producing an extraction JSON, score and cache it using <code>datasheet_score</code> and <code>datasheet_extract_cache</code> modules. Extractions are stored in <code>datasheets/extracted/<MPN>.json</code> and reused across reviews. The **<code>datasheets</code> skill** owns the full extraction pipeline (schema, page selection, scoring rubric, consumer API) — see <code>skills/datasheets/SKILL.md</code> and its reference guides.

**What to extract from each datasheet** (note page/section/figure/equation numbers for citations):
- Pin function table (pin number → name → function)
- Absolute maximum ratings (voltage, current, temperature — including max continuous current through VCC/GND pins, which constrains inrush)
- Recommended application circuit and required external components
- Required component values (and the equations that derive them)
- Thermal characteristics

**For passives:** While individual resistor/capacitor datasheets are rarely needed, verify the component values against the IC datasheets that specify them. The IC's datasheet says "use a 10µF input cap" — verify the schematic actually has 10µF there, not 1µF.

**Anti-pattern: verification without datasheets.** The most common failure mode in design review is verifying component connections against KiCad library symbols instead of manufacturer datasheets. This is circular — if the library symbol has a wrong pin mapping, the schematic, PCB, and analyzer output will all agree with each other (and with the wrong pinout). Only the datasheet reveals the error. This is especially dangerous for custom/community library symbols (e.g., <code>sacmap:TPS61023</code>) where there's no upstream KiCad library as a secondary check. If you find yourself verifying a pinout by reading the <code>.kicad_sym</code> file or the analyzer's pin data and confirming it matches the schematic — stop. That's a consistency check, not a correctness check. Open the actual PDF datasheet, find the pin function table, and verify against that. Cite the datasheet page/section/figure number in your report so the designer can confirm your work.

### Schematic + PCB Cross-Reference

When both files exist, cross-reference them. This catches the most expensive bugs — swapped pins, missing nets, and footprint mismatches pass DRC/ERC but produce non-functional boards.

1. **Component count**: Schematic count (excluding power symbols) vs PCB footprint count.
2. **Net consistency**: Verify schematic net names appear in PCB net declarations. Missing nets suggest incomplete routing or un-synced changes.
3. **Pin-net assignments**: Compare schematic pin-to-net mapping against PCB pad-to-net mapping. Mismatches reveal swapped pins or library errors. Higher-risk areas:
   - Custom/community library symbols (may not match datasheet pinout)
   - Multi-unit symbols (op-amps, gate arrays) — unit-to-pin assignment errors
   - QFN/BGA packages — pad numbering mistakes
   - Transistors without MPNs — pinout ambiguity (see verification step 3)
   - Polarized components — anode/cathode orientation
   - Connectors — pin 1 orientation
4. **Footprint match**: Schematic <code>Footprint</code> property vs actual PCB footprint (e.g., SOT-23 vs SOT-23-5).
5. **DNP consistency**: DNP components in schematic should not have routing on PCB.
6. **Value/MPN consistency**: Values and MPNs match between schematic and PCB properties.

The PCB analyzer's <code>sch_path</code>, <code>sch_sheetname</code>, and <code>sch_sheetfile</code> fields in each footprint enable automated cross-referencing.

### Diff-Aware Design Comparison

Compare two analysis JSON outputs to see what changed between design revisions (e.g., base branch vs PR, v1 vs v2). Use when the user says things like "compare designs", "what changed", "diff my schematic", "show changes from main", or "diff base vs head". Full reference: <code>references/diff-analysis.md</code>.

<pre><code>
# Compare two schematic analysis outputs (JSON to stdout)
python3 <skill-path>/scripts/diff_analysis.py base.json head.json

# Human-readable text output
python3 <skill-path>/scripts/diff_analysis.py base.json head.json --text

# Write to file, custom threshold (ignore <2% deltas)
python3 <skill-path>/scripts/diff_analysis.py base.json head.json --output diff.json --threshold 2.0

# Ignore small percentage changes (e.g., rounding noise)
python3 <skill-path>/scripts/diff_analysis.py base.json head.json --threshold 5.0 --text
</code></pre>

Auto-detects analyzer type (schematic, PCB, EMC, SPICE). Reports:
- **Components**: new, removed, value/footprint/MPN changes
- **Signal analysis**: parameter shifts per detection type, driven by <code>detection_schema.SCHEMAS</code> identity and value fields
- **BOM**: added/removed line items, quantity changes
- **Connectivity/ERC**: new/resolved single-pin nets, floating nets, multi-driver nets, ERC warnings
- **EMC findings**: new/resolved findings with severity, risk score delta, per-net score changes
- **SPICE results**: status transitions (pass->fail regressions, fail->pass fixes), Monte Carlo concern changes
- **Severity classification**: <code>none</code> (no changes), <code>minor</code> (statistics only), <code>major</code> (component/signal/finding changes), <code>breaking</code> (SPICE regressions, new CRITICAL EMC findings, new ERC warnings)

Also used programmatically by <code>analysis_cache.should_create_new_run()</code> to decide whether new outputs warrant a new timestamped run folder.

### Thermal Hotspot Estimation

Estimates junction temperatures of power-dissipating components by combining schematic power data with PCB thermal infrastructure (copper pour, thermal vias, package type). Use when the user says "check thermals", "thermal analysis", "will this overheat", "junction temperature", "power dissipation", or "thermal design".

<pre><code>
# Recommended: integrate into the current run
python3 <skill-path>/scripts/analyze_thermal.py \
    -s analysis/<run_id>/schematic.json \
    -p analysis/<run_id>/pcb.json \
    --analysis-dir analysis/

# Human-readable text report
python3 <skill-path>/scripts/analyze_thermal.py -s schematic.json -p pcb.json --text

# Custom ambient temperature (default: 25°C), one-off output file
python3 <skill-path>/scripts/analyze_thermal.py -s schematic.json -p pcb.json --ambient 40 -o thermal.json
</code></pre>

Models each power component (LDO, switching regulator, shunt resistor) as a point heat source. Computes Tj = T_ambient + P_diss × Rθ_JA_effective, where Rθ_JA comes from a package lookup table (SOT-223: 60°C/W, QFN-5x5: 25°C/W, etc.) and is corrected for PCB thermal vias and copper pour. Rules:

| Rule | Condition | Severity |
|------|-----------|----------|
| TS-001 | Tj exceeds absolute maximum | CRITICAL |
| TS-002 | Tj within 15°C of absolute maximum | HIGH |
| TS-003 | Tj > 85°C (may affect nearby passives) | MEDIUM |
| TS-004 | P > 0.5W with no thermal vias | MEDIUM |
| TS-005 | Significant power, within safe limits | INFO |
| TP-001 | MLCC within 10mm of hot component | LOW |
| TP-002 | Electrolytic cap within 10mm of hot component | MEDIUM |

Thermal findings and assessments include the rich format envelope (detector, rule_id, summary, evidence_source, report_context). Rule IDs: TS-001..005 (safety), TP-001..002 (proximity), TH-DET (assessments).

### Interactive "What-If" Parameter Sweep

Instantly see the impact of component value changes on circuit behavior without re-running the full analyzer. Use when the user says "what if I change", "what happens if", "try a different value", "swap R5 to 4.7k", "parameter sweep", "what value gives me X", or wants to explore design trade-offs. Full reference: [<code>references/what-if.md</code>](references/what-if.md).

<pre><code>
# Single value change
python3 <skill-path>/scripts/what_if.py analysis.json R5=4.7k --text

# Sweep: comma list or log range
python3 <skill-path>/scripts/what_if.py analysis.json R5=1k,2.2k,4.7k,10k --text
python3 <skill-path>/scripts/what_if.py analysis.json R5=1k..100k:10 --text

# Tolerance corner analysis (±5% worst-case)
python3 <skill-path>/scripts/what_if.py analysis.json R5=4.7k+-5% C3=100n+-10% --text

# Find the right value: inverse solver with E-series snapping
python3 <skill-path>/scripts/what_if.py analysis.json --fix voltage_dividers[0] --target 3.3 --text
python3 <skill-path>/scripts/what_if.py analysis.json --fix rc_filters[0] --target 1000 --text

# EMC impact preview
python3 <skill-path>/scripts/what_if.py analysis.json C3=1u --emc --text

# SPICE re-simulation on affected subcircuits
python3 <skill-path>/scripts/what_if.py analysis.json R5=4.7k --spice --text

# Export patched JSON for further analysis (EMC, thermal, diff)
python3 <skill-path>/scripts/what_if.py analysis.json R5=4.7k --output patched.json
</code></pre>

Patches component values in the analyzer JSON, recalculates derived fields (filter cutoff, divider ratio, opamp gain, crystal load, current sense range, regulator Vout), and shows before/after comparison with percentage deltas. Supports single changes, multi-point sweeps (comma or log-range), tolerance corner analysis, inverse fix suggestions with E-series snapping, EMC impact preview, PCB parasitic awareness (auto-discovered or via <code>--pcb</code>), and SPICE re-verification.

### Findings Summary

Summarises findings across all analyzers in a run. Use when the user wants a top-N list, a severity-filtered view, or a machine-readable roll-up without reading individual JSON files. Reads the current run from <code>analysis/manifest.json</code>.

<pre><code>
# Top findings from the current run (default: top 20)
python3 <skill-path>/scripts/summarize_findings.py analysis/

# Limit to top 10 high-severity findings
python3 <skill-path>/scripts/summarize_findings.py analysis/ --top 10 --severity high

# JSON output for programmatic consumption
python3 <skill-path>/scripts/summarize_findings.py analysis/ --json

# Summarise a specific run by ID
python3 <skill-path>/scripts/summarize_findings.py analysis/ --run <run_id>
</code></pre>

Flags: <code>--top N</code> (default 20), <code>--severity</code> (filter to <code>critical</code>/<code>high</code>/<code>warning</code>/<code>info</code>), <code>--run</code> (explicit run ID instead of latest), <code>--json</code> (machine-readable output).

### Component Lifecycle & Temperature Audit

Queries distributor APIs to check component lifecycle status (active, NRND, EOL, obsolete) and operating temperature range coverage. Use when the user says "check for obsolete parts", "lifecycle audit", "are any parts end of life", "temperature audit", "will this work at industrial temp range", or during production readiness reviews.

<pre><code>
# Basic lifecycle check
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json

# With temperature range validation (preset or custom)
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json --temp-range industrial
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json --temp-range="-40,105"

# Query specific distributors only
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json --only digikey,lcsc

# Search for replacement parts when EOL/NRND found
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json --suggest-alternatives

# Save results
python3 <skill-path>/scripts/lifecycle_audit.py analysis.json --output lifecycle.json
</code></pre>

Reads the analyzer JSON BOM section, extracts unique MPNs, queries distributors (LCSC no-auth, DigiKey, element14, Mouser) for lifecycle status and operating temperature. Temperature presets: <code>commercial</code> (0/70°C), <code>industrial</code> (-40/85°C), <code>extended</code> (-40/105°C), <code>automotive</code> (-40/125°C), <code>military</code> (-55/125°C). Also checks datasheet extraction cache for temperature data before making API calls.

The lifecycle audit produces rich format findings: LC-001 (obsolete/discontinued), LC-002 (last time buy), LC-003 (NRND), LC-004 (unknown status), LC-005 (single source), LC-006 (long lead time), LT-001 (temperature violation). When <code>--lifecycle</code> is NOT passed (the default), <code>analyze_schematic.py</code> emits an <code>LC-007</code> info finding noting that the audit was skipped — keeps the gap explicit in the report's findings list instead of being a silent omission.

**Requires network access** — unlike the core analyzers, this script calls distributor APIs. Same environment variables as the distributor skills (DIGIKEY_CLIENT_ID/SECRET, MOUSER_SEARCH_API_KEY, ELEMENT14_API_KEY). LCSC requires no credentials.

### Schematic Analyzer Rule IDs

All schematic rule findings appear in <code>findings[]</code>. The following rule IDs are produced by the schematic analyzer:

| Rule | Detector | Condition | Severity |
|------|----------|-----------|----------|
| SS-001 | <code>audit_sourcing_gate</code> | MPN coverage < 50% | high |
| SS-002 | <code>audit_sourcing_gate</code> | MPN coverage 50–80% | warning |
| SS-003 | <code>audit_sourcing_gate</code> | MPN coverage 80–100% | info |
| NT-001 | <code>analyze_connectivity</code> | Single-pin net: signal pin | warning |
| NT-001 | <code>analyze_connectivity</code> | Single-pin net: power_out or passive pin | info |
| RS-001 | <code>audit_rail_sources</code> | Rail has no declared source (no power_out pin, no PWR_FLAG, no bridged-jumper or regulator output) | warning |
| RS-002 | <code>audit_rail_sources</code> | Rail depends on user closing an open jumper | high |
| RS-003 | <code>audit_rail_sources</code> | Rail sourced indirectly via a bridged-by-default solder jumper or ferrite — functional but consider adding PWR_FLAG | info |
| LB-001 | <code>detect_label_aliases</code> | Net has >= 2 distinct global/hierarchical labels (power nets excluded) | info |
| PP-001 | <code>audit_power_pin_dc_paths</code> | IC power_in pin reaches a rail only through a capacitor (2-hop BFS) | high |

SS-001 is a pre-fab blocker — a <code>high</code> finding that should be resolved before ordering. NT-001 severity depends on pin type: signal pins (digital I/O, bidirectional) are <code>warning</code>; power_out and passive pins are <code>info</code>. RS-001 is reserved for the "no source at all" case (warning); the softer "sourced via bridged jumper / ferrite" case has its own rule_id RS-003 (info) so reviewers and CI gates can filter the two independently. PP-001 uses a 2-hop BFS over the net graph, rejecting capacitor edges, to confirm a direct DC path from a power rail to each IC power_in pin. PP-001 demotes to <code>info</code> on module-internal LDO rails (<code>VDDPLL_*</code>, <code>VDDA_INT_*</code>, <code>VDDCORE_*</code>, <code>VCAP</code>, <code>VDDREG</code>) where decoupling-only is the correct topology.

## Deep Review Pass

After the analyzers have run, perform the Deep Review pass: a per-IC
comparison of actual usage against datasheet requirements. The
analyzers provide facts; this pass is where the judgment happens.

For each IC in the design:

1. Pull its datasheet facts — extraction cache first
   (<code>datasheets/extracted/<MPN>.json</code>), PDF fallback (read it
   directly). If neither exists, record an info finding
   "<REF> unverified — no datasheet available" and move on.
2. Compare against actual usage in the analyzer JSON: supply voltages
   vs ratings, pin functions vs connections, required externals
   (inductors, caps, resistors) vs what the schematic has, thermal
   context. Derive the checks from the datasheet — every part has
   its own.
3. Where a limit needs computing, write a disposable helper script
   under <code>analysis/helpers/</code> that reads the analyzer JSON and prints
   the numbers. Cite script + result as evidence.
4. Record findings in <code>analysis/deep_review.json</code> (shape:
   <code>skills/kicad/review/schemas/deep_review.schema.json</code>). Every
   finding needs an evidence block: at least one design anchor
   (component/net/pin) and at least one source (datasheet quote
   and/or computation).

Then check interacting pairs: shared rails (sequencing, combined load), bus partners (voltage levels, pull-up ownership), thermal neighbors. Freestyle digging is encouraged — follow what looks wrong.

Validate before reporting:

<pre><code>
python3 skills/kicad/review/scripts/deep_review_gate.py \
    analysis/deep_review.json --analysis-dir analysis/
</code></pre>

The gate verifies citations, stamps finding_ids, and moves failures
to <code>quarantined[]</code> with reasons. Fix or accept quarantined entries;
the report renders them as unverified claims.

Re-review: if <code>analysis/deep_review.json</code> already exists from a prior
review, copy it aside first and open the new pass with a diff —
report fixed / still-open / new in the Previous Review Delta section:

<pre><code>
cp analysis/deep_review.json analysis/deep_review.prev.json
# ... run the pass, then the gate ...
python3 skills/kicad/scripts/diff_analysis.py \
    analysis/deep_review.prev.json analysis/deep_review.json --text
</code></pre>

Big BOM: chunk by subsystem, or fan out per IC-group with subagents. See <code>references/deep-review.md</code> for comparison heuristics per part class, pair-check patterns, and helper-script conventions. <code>analysis/design_context.json</code> (if present) steers priorities — e.g. automotive tightens derating attention. Optional; never block on it.

## Reference Files

Detailed methodology and format documentation lives in reference files. Read these as needed — they provide deep-dive content beyond what the scripts output automatically.

| Reference | Lines | When to Read |
|-----------|-------|-------------|
| <code>deep-review.md</code> | — | Deep Review pass: per-part-class comparison heuristics, pair checks, helper-script conventions, fan-out |
| <code>schematic-analysis.md</code> | 1133 | Deep schematic review: datasheet validation, design patterns, error taxonomy, tolerance stacking, GPIO audit, motor control, battery life, supply chain |
| <code>pcb-layout-analysis.md</code> | 447 | Advanced PCB: impedance calculations, differential pairs, return paths, copper balance, edge clearance, copper-sensitive components (capacitive touch, antennas), custom analysis scripts |
| <code>output-schema.md</code> | 293 | Full analyzer JSON schema with field names, types, and common extraction patterns |
| <code>file-formats.md</code> | 379 | Manual file inspection: S-expression structure, field-by-field docs for all KiCad file types, version detection |
| <code>gerber-parsing.md</code> | 729 | Gerber/Excellon format details, X2 attributes, analysis techniques |
| <code>pdf-schematic-extraction.md</code> | 315 | PDF schematic analysis: extraction workflow, notation conventions, KiCad translation |
| <code>supplementary-data-sources.md</code> | 288 | Legacy KiCad 5 data recovery: netlist parsing, cache library, PCB cross-reference |
| <code>net-tracing.md</code> | 120 | Manual net tracing: coordinate math, Y-axis inversion, rotation transforms |
| <code>manual-schematic-parsing.md</code> | 289 | Fallback when schematic script fails |
| <code>manual-pcb-parsing.md</code> | 467 | Fallback when PCB script fails |
| <code>manual-gerber-parsing.md</code> | 621 | Fallback when Gerber script fails |
| <code>report-generation.md</code> | 614 | Report template (critical findings at top), analyzer output field reference (schematic/PCB/gerber), severity definitions, writing principles, domain-specific focus areas, known analyzer limitations |
| <code>standards-compliance.md</code> | 638 | IPC/IEC standards tables: conductor spacing (IPC-2221A Table 6-1), current capacity (IPC-2221A/IPC-2152), annular rings, hole sizes, impedance, via protection (IPC-4761), creepage/clearance (ECMA-287/IEC 60664-1). Consider for all boards; auto-trigger for professional/industrial designs, high voltage, mains input, or safety isolation. |
| <code>oshwa-certification.md</code> | — | OSHWA open-source hardware certification readiness: editable KiCad sources, public documentation, licensing, version registration, certification-mark use. Read when the user asks about OSHWA certification or an open-source hardware release — a documentation/licensing audit with explicit approval gates, not an electrical check |
| <code>design-intent.md</code> | — | Design intent resolution, target market / certification / power constraints that gate findings by context |
| <code>diff-analysis.md</code> | — | How <code>diff_analysis.py</code> compares two analyzer runs and emits severity-ranked change reports |
| <code>what-if.md</code> | — | How <code>what_if.py</code> patches component values, recalculates derived fields, and suggests fixes for feedback dividers / crystal load caps / cap derating |
| <code>config-reference.md</code> | — | <code>.kicad-happy.json</code> schema — project config for analysis cache, suppressions, design intent, risk scoring |
| <code>datasheet-verification.md</code> | — | Automated cross-check of schematic connections against structured datasheet extractions (pin voltage, required externals, decoupling adequacy) |

For script internals, data structures, signal analysis patterns, and batch test suite documentation, see <code>scripts/README.md</code>.

## File Types Quick Reference

| Extension | Format | Purpose |
|---|---|---|
| <code>.kicad_pro</code> | JSON | Project settings, net classes, DRC/ERC severity, BOM fields |
| <code>.kicad_sch</code> | S-expr | Schematic sheet (symbols, wires, labels, hierarchy) |
| <code>.kicad_pcb</code> | S-expr | PCB layout (footprints, tracks, vias, zones, board outline) |
| <code>.kicad_sym</code> | S-expr | Symbol library (schematic symbols with pins, graphics) |
| <code>.kicad_mod</code> | S-expr | Single footprint (in <code>.pretty/</code> directory) |
| <code>.kicad_dru</code> | Custom | Custom design rules (DRC constraints) |
| <code>fp-lib-table</code> / <code>sym-lib-table</code> | S-expr | Library path tables |
| <code>.sch</code> / <code>.lib</code> / <code>.dcm</code> | Legacy | KiCad 5 schematic, symbol library, descriptions |
| <code>.net</code> / <code>.xml</code> | S-expr/XML | Netlist export, BOM export |
| <code>.gbr</code> / <code>.g*</code> / <code>.drl</code> | Gerber/Excellon | Manufacturing files (copper, mask, silk, outline, drill) |

For version detection and detailed field-by-field format documentation, read <code>references/file-formats.md</code>. On Flatpak KiCad installs <code>kicad-cli</code> (native DRC/ERC reports, file exports) is not on PATH — invoke it as <code>flatpak run --command=kicad-cli org.kicad.KiCad <args></code>.

## Analysis Strategies

### Deep Schematic Analysis

For a thorough datasheet-driven schematic review — identifying subcircuits, fetching datasheets, validating component values against manufacturer recommendations, comparing against common design patterns, detecting errors, and suggesting improvements — read <code>references/schematic-analysis.md</code>. Use this reference whenever the user asks to review, validate, or analyze a schematic in depth.

**Fetching datasheets**: When the analysis requires datasheet data, use the DigiKey API as the preferred source (see the <code>digikey</code> skill) — it returns direct PDF URLs via the <code>DatasheetUrl</code> field without web scraping. Search by MPN from the schematic's component properties. Fall back to web search only for parts not on DigiKey.

### Deep PCB Analysis

For advanced layout analysis beyond what the PCB analyzer script provides — impedance calculations from stackup parameters, DRC rule authoring, power electronics design review techniques, differential pair validation, return path analysis, copper balance assessment, board edge clearance rules, and manual script-writing patterns — read <code>references/pcb-layout-analysis.md</code>.

Most routine PCB analysis (via types, annular ring, placement, connectivity, thermal vias, current capacity, signal integrity, DFM scoring, tombstoning risk, thermal pad vias) is handled automatically by <code>analyze_pcb.py</code>. Use the reference for deeper manual investigation.

### Design Intent

For interpreting auto-detected design intent and calibrating review severity by product class and target market (hobby vs consumer vs industrial vs medical vs automotive vs aerospace), read <code>references/design-intent.md</code>. Check the <code>design_intent</code> object in analysis output to understand the design context.

### Probing Analyzer JSON

During a review you will often run one-off <code>python3 -c "import json; ..."</code> probes to inspect analyzer output (pin-nets, rail voltages, specific finding contents, etc.). Two practices that materially improve the user's ability to follow along:

**Announce what you're checking before each probe.** One concise sentence before the script — not after, not in a comment inside the script. The user should be able to read only the narrative and understand the review flow without opening every tool call.

- Bad: <code>[Bash] python3 -c "import json; d = json.load(open('analysis/.../schematic.json')); print([...])"</code> with no surrounding prose.
- Good: "Checking whether U3's EN pin is tied to +BATT directly or through a divider." then the probe.
- Good: "Verifying the detected TPS61023 topology matches the datasheet (buck-boost expected)." then the probe.

The narrative matters most for probes that investigate *why* something looks wrong — those are the moments a user loses context fastest.

**Defensive patterns for JSON-shape uncertainty.** Analyzer output has heterogeneous shapes (lists of dicts, dicts keyed by ref, optional sections, nested paths). Scripts that assume the wrong shape crash mid-probe.

- Before slicing, check type: <code>x[:3]</code> on a dict raises <code>KeyError: slice(None, 3, None)</code>. Use <code>list(x.values())[:3]</code> or <code>list(x.items())[:3]</code> for dicts.
- Before <code>min()</code> / <code>max()</code>, check non-empty: <code>min([])</code> raises <code>ValueError: min() iterable argument is empty</code>. Use <code>min(items, default=None)</code> or guard with <code>if items:</code>.
- Use <code>.get("key", default)</code> not <code>["key"]</code> when a section may be absent (many sections are optional based on what the analyzer found).
- <code>isinstance(x, list)</code> vs <code>isinstance(x, dict)</code> — <code>components</code> is a list of dicts, <code>nets</code> is a dict keyed by net name. Check the schema reference before iterating.
- When a finding field can be a list of strings OR a list of dicts (rare but happens in legacy-shape sections), handle both: <code>r = c if isinstance(c, str) else c.get("reference", "")</code>.
- Pads/components with missing data: <code>pad.get("abs_x")</code> can be <code>None</code>; guard before arithmetic.

Small investment, much lower friction.

For probing the raw <code>.kicad_sch</code> / <code>.kicad_pcb</code> files themselves (not the analyzer JSON), use <code><skill-path>/scripts/sexp_parser.py</code>: <code>parse(text)</code> and <code>parse_file(path)</code> return the document as nested Python lists; <code>find_all(node, kw)</code>, <code>find_first(node, kw)</code>, and <code>get_value(node, kw)</code> navigate it.

### Quick Review Checklists

**Schematic** — verify: decoupling caps on every IC VCC/GND pair, I2C pull-ups, reset pin circuits, unconnected pins have no-connect markers, consistent net naming across sheets, ESD protection on external connectors, power sequencing (EN/PG), adequate bulk capacitance.

**PCB** — verify: power trace widths for current (IPC-2221), via current capacity, creepage/clearance for high voltage, decoupling cap proximity to IC power pins, continuous ground plane (no splits under signals), controlled impedance traces (USB/DDR), board outline closed polygon, silkscreen readability, thermal via count for every exposed-pad IC (report the count and compare against the datasheet's recommended range — this is one of the most common QFN/DFN layout errors), keepout zone enforcement for copper-sensitive components (touch pads and antennas — confirming copper absence isn't enough because it could be accidental; check that keepout zones exist as DRC rules), differential pair length deltas with protocol-specific tolerance (compute the delta and cite the spec — raw lengths alone don't tell the designer if there's a problem), pad-to-net cross-reference at PCB level for all ICs/transistors/connectors (catches library footprint pin numbering errors that are invisible to DRC/ERC — the most dangerous class of PCB bug). Consider <code>references/standards-compliance.md</code> for IPC/IEC standard values — conductor spacing and current capacity are relevant for most boards; creepage/clearance and via protection apply to mains-connected or safety-isolated designs.

**Common bugs (ranked by board-killing potential)**: swapped IC pins (library symbol vs datasheet pinout — invisible to DRC/ERC), transistor pinout ambiguity (SOT-23 without MPN — symbol assumes a pin ordering that may not match the real part; assess plausibility against common conventions when verification isn't possible), wrong footprint pad numbering, missing nets from un-synced schematic→PCB, wrong package variant (SOT-23 vs SOT-23-5), floating digital inputs, missing bulk caps, reversed polarity, incorrect feedback divider values, wrong crystal load caps, USB impedance mismatch, QFN thermal pad missing vias, connector pinout errors, unusual passive values (a value that's technically valid but uncommon for the application — e.g., a non-standard pull-up resistance, an unusual decoupling capacitor value).

### Report Generation

When producing a design review report, read <code>references/report-generation.md</code> for the standard report template, severity definitions, writing principles, and domain-specific focus areas. The report format covers: overview, component summary, power tree, analyzer verification (spot-checks), signal/power/design analysis review, quality & manufacturing, prioritized issues table, positive findings, and known analyzer gaps. Always cross-reference analyzer output against the raw schematic before reporting findings.

### Design Comparison
When comparing two designs, diff: component counts/types, net classes/design rules, track widths/via sizes, board dimensions/layer count, power supply topology, KiCad version differences.

## Security Architecture

All analysis scripts process untrusted input (user-provided KiCad files,
third-party PDF datasheets, distributor API responses). The parsing architecture
mitigates prompt injection and code execution risks:

- **Deterministic parsers**: S-expression files (<code>.kicad_sch</code>, <code>.kicad_pcb</code>) are
  parsed by a dedicated recursive-descent parser (<code>sexp_parser.py</code>) — not
  <code>eval()</code>, <code>exec()</code>, or any code execution primitive. The parser produces
  Python lists/strings only.
- **Structured JSON boundary**: All analyzer output is structured JSON with a
  fixed schema. External content (component values, net names, datasheet text)
  is treated as data fields, never as instructions or code.
- **PDF processing**: Datasheet PDFs are processed by <code>pdftotext</code> (external
  binary, list-based args — no shell injection) and page content is passed to
  the LLM for structured extraction. Extracted data is validated against a
  5-dimension quality rubric before caching.
- **No shell commands from input**: No analyzer constructs shell commands from
  file content. Subprocess calls use list-based arguments exclusively.
- **Read-only by default**: Analysis scripts never modify input files. BOM
  write-back requires an explicit <code>--write</code> flag.
- **Distributor API scope**: Network requests are limited to known distributor
  APIs (DigiKey, Mouser, LCSC, element14) for datasheet downloads and component
  lookups. Only MPNs are sent — no design data leaves the local machine.
