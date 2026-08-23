# KiCad Analysis Scripts — Developer Reference

This directory contains the core analysis scripts, shared utilities, the S-expression parser, and the rich-finding/trust-summary infrastructure. Each analyzer outputs a structured JSON envelope for the AI agent to consume during design reviews.

| Script | Input | Size | Purpose |
|--------|-------|------|---------|
| <code>analyze_schematic.py</code> | <code>.kicad_sch</code> / <code>.sch</code> | ~9,300 LOC | Component extraction, net building, subcircuit detection, signal/power/BOM/DFM analysis, audit detectors |
| <code>analyze_pcb.py</code> | <code>.kicad_pcb</code> | ~6,600 LOC | Footprint inventory, routing, signal integrity, power, thermal, placement, manufacturing, DFM, union-find connectivity graph, assembly/DFM checks |
| <code>analyze_gerbers.py</code> | Gerber dir (<code>.gbr</code>/<code>.drl</code>) | ~1,400 LOC | Layer completeness, drill holes, apertures, coordinate alignment, X2 attributes |
| <code>analyze_thermal.py</code> | schematic + PCB JSON | ~910 LOC | Junction-temperature estimator with package θJA, thermal via correction, proximity warnings |
| <code>cross_analysis.py</code> | schematic + PCB JSON | ~430 LOC | Cross-domain checks: CC-001 connector current, EG-001 ESD gaps, DA-001 decoupling adequacy, XV-001..003, PCB intelligence (NR/RP/TW/PS/VS/DP-005) |
| <code>lifecycle_audit.py</code> | schematic JSON + distributor API | ~855 LOC | Component obsolescence, temperature audit (LC-001..006, LT-001) |
| <code>sexp_parser.py</code> | — | ~220 LOC | S-expression parser shared by schematic and PCB analyzers |
| <code>kicad_utils.py</code> | — | ~860 LOC | Shared utilities: component classification, value parsing, net detection, switching-frequency table, Vref lookup |
| <code>kicad_types.py</code> | — | ~110 LOC | Typed dataclass (<code>AnalysisContext</code>) shared across detectors |
| <code>signal_detectors.py</code> | — | ~4,400 LOC | Core signal path detectors (regulators, filters, opamps, dividers, crystals, transistors, bridges, protection), plus v1.3 audit detectors (RS-001, LB-001, PP-001) |
| <code>domain_detectors.py</code> | — | ~6,100 LOC | Domain-specific detectors (RF, Ethernet, HDMI, memory, BMS, battery chargers, motor drivers, wireless modules, etc.) |
| <code>validation_detectors.py</code> | — | ~1,000 LOC | Validation detectors (PU-001, VM-001, PR-001..004, PS-001, LR-001, FS-001) |
| <code>finding_schema.py</code> | — | ~330 LOC | <code>make_finding()</code> factory, <code>Det.*</code> constants, <code>get_findings()</code> / <code>group_findings()</code> helpers, <code>trust_summary</code> aggregation, <code>sort_findings()</code> determinism |
| <code>output_filters.py</code> | — | ~460 LOC | Stage/audience filtering (<code>--stage schematic/layout/pre_fab/bring_up</code>, <code>--audience designer/reviewer/manager</code>) |
| <code>pcb_connectivity.py</code> | — | ~300 LOC | Union-find over pads/tracks/vias/zone fills for per-net island detection (used by <code>analyze_pcb.py --full</code>) |
| <code>project_config.py</code> | — | ~870 LOC | <code>.kicad-happy.json</code> loader, suppression matching, design intent resolution |
| <code>analysis_cache.py</code> | — | ~510 LOC | Analysis-folder convention, manifest-based run tracking, SHA-256 staleness detection |
| <code>diff_analysis.py</code> | two analyzer JSONs | ~950 LOC | Diff-aware design comparison (component/signal/EMC/SPICE) |
| <code>what_if.py</code> | analyzer JSON + patch spec | ~1,500 LOC | Parameter sweep + automated fix suggestions (inverse solvers with E-series snapping) |
| <code>summarize_findings.py</code> | analysis/manifest.json | ~200 LOC | Cross-run severity × count rollup |

Detailed methodology documentation for each analyzer:
- <code>methodology_schematic.md</code> — parsing pipeline, net building, component classification, detector inventory
- <code>methodology_pcb.md</code> — extraction, union-find connectivity, DFM scoring, thermal/placement/SI analysis, assembly/DFM checks
- <code>methodology_gerbers.md</code> — RS-274X/Excellon parsing, X2 attributes, layer identification, completeness/alignment checks

---

## sexp_parser.py

Parses KiCad's Lisp-like S-expression format into nested Python lists. Used by both <code>analyze_schematic.py</code> and <code>analyze_pcb.py</code>.

### API

| Function | Purpose |
|----------|---------|
| <code>parse_file(path)</code> | Parse a <code>.kicad_sch</code> or <code>.kicad_pcb</code> file → nested lists |
| <code>find_all(node, keyword)</code> | Find direct children starting with keyword |
| <code>find_first(node, keyword)</code> | Find first direct child starting with keyword |
| <code>find_deep(node, keyword)</code> | Recursive search at any depth |
| <code>get_value(node, keyword)</code> | Get value from <code>(keyword value)</code> pair |
| <code>get_property(node, prop_name)</code> | Get value from <code>(property "name" "value")</code> |
| <code>get_at(node)</code> | Get <code>(x, y, angle)</code> from <code>(at ...)</code> node |
| <code>get_xy(node)</code> | Get <code>(x, y)</code> from <code>(xy ...)</code> node |

**Design note**: The parser is intentionally simple — no schema validation, no type coercion beyond strings. All values come back as strings; callers convert to <code>float</code>/<code>int</code> as needed. This makes it robust against KiCad version differences.

**Pitfall**: <code>find_all</code> and <code>find_first</code> only search direct children. For nested structures, use <code>find_deep</code> — but be aware it can return matches from unrelated subtrees.

---

## analyze_pcb.py

Parses <code>.kicad_pcb</code> files (KiCad 5 <code>module</code> and KiCad 6+ <code>footprint</code> formats).

### Pipeline

```
.kicad_pcb file
    |
    v
 EXTRACTION (core data)
extract_layers()            -- Layer stack definitions (incl. jumper layers)
extract_setup()             -- Thickness, stackup, copper finish, paste ratio, teardrops
extract_nets()              -- Net number → name mapping
extract_footprints()        -- Footprints with pads, courtyards, attrs, sch cross-ref
extract_tracks()            -- Track segments and arcs with width/layer stats
extract_vias()              -- Vias with type, free flag, tenting
extract_zones()             -- Zones with fill areas, keepouts, priority, pad connection
extract_board_outline()     -- Edge.Cuts geometry, bounding box
extract_board_metadata()    -- Title block, properties, paper size
extract_dimensions()        -- Designer-placed dimension annotations
extract_groups()            -- Designer-defined component/routing groups
extract_net_classes()       -- Net class definitions (KiCad 5 legacy)
extract_silkscreen()        -- Board-level text on SilkS/Fab layers
    |
    v
 ANALYSIS (derived facts)
analyze_connectivity()      -- Unrouted nets (zone-aware)
analyze_net_lengths()       -- Per-net trace length (segments + arcs)
analyze_power_nets()        -- Power net routing summary
analyze_decoupling_placement() -- Cap-to-IC distance
analyze_ground_domains()    -- AGND/DGND split detection
analyze_current_capacity()  -- Track widths per net for IPC-2221
analyze_vias()              -- Type breakdown, annular ring, via-in-pad, fanout, current
analyze_thermal_vias()      -- Zone stitching density, thermal pad detection
analyze_layer_transitions() -- Signal net layer changes (ground return paths)
analyze_placement()         -- Courtyard overlaps, edge clearance, density
analyze_trace_proximity()   -- Spatial grid crosstalk assessment (optional)
compute_statistics()        -- Summary counts
    |
    v
JSON output (~50-300KB depending on board complexity)
```

### Key Design Decisions

- **Pad positions are absolute**: Pad <code>(at)</code> is relative to footprint; the code rotates by footprint angle and adds footprint position to compute absolute coordinates.
- **Footprint summary omits raw pads by default**: The JSON output includes <code>connected_nets</code> per footprint instead of full pad arrays. Use <code>--full</code> for individual track/via data.
- **Zone fill areas computed without storing coordinates**: Shoelace formula applied directly to parsed S-expression nodes — the parse tree is already in memory, we just iterate and accumulate. Avoids the massive memory cost of storing filled polygon coordinate arrays.
- **Keepout zones distinguished from copper zones**: Zones with <code>(keepout ...)</code> blocks are flagged with <code>is_keepout: true</code> and their restriction types (tracks, vias, pads, copperpour, footprints).
- **Extended footprint attributes**: Parses full <code>(attr ...)</code> node including <code>dnp</code>, <code>board_only</code>, <code>exclude_from_bom</code>, <code>exclude_from_pos_files</code>. Also extracts schematic cross-reference (<code>path</code>, <code>sheetname</code>, <code>sheetfile</code>), net ties, 3D model references, and manufacturer/MPN properties.
- **Custom pad copper area**: Pads with <code>custom</code> shape have their <code>(primitives (gr_poly ...))</code> areas computed via shoelace, giving accurate copper area for power MOSFET pads.
- **Free vias identified**: Vias with <code>(free yes)</code> are flagged — typically stitching or thermal vias not anchored to tracks.
- **Pin function/type carried from schematic**: Pad-level <code>pinfunction</code> and <code>pintype</code> enable power-pin vs signal-pin differentiation without needing the schematic.
- **KiCad 5 compatibility**: Handles <code>(module ...)</code>, <code>(fp_text reference ...)</code>, <code>(net_class ...)</code>, and <code>(dimension ...)</code> in addition to KiCad 6+ equivalents.
- **Unrouted detection**: Zone-aware — nets routed only through copper pours are not flagged as unrouted.
- **Facts over judgement**: Analysis functions provide raw facts (track widths, via counts, distances) rather than pass/fail verdicts, enabling flexible higher-level analysis.

### Usage

```bash
python3 analyze_pcb.py board.kicad_pcb                    # JSON to stdout
python3 analyze_pcb.py board.kicad_pcb --output out.json  # JSON to file
python3 analyze_pcb.py board.kicad_pcb --compact          # Minified JSON
python3 analyze_pcb.py board.kicad_pcb --full              # Include individual tracks/vias
python3 analyze_pcb.py board.kicad_pcb --proximity        # Add crosstalk proximity analysis
```

---

## analyze_gerbers.py

Parses a directory of Gerber RS-274X files and Excellon drill files. Does NOT render the gerbers — it extracts metadata, counts, and performs sanity checks.

### Pipeline

```
gerber directory
    |
    v
parse_gerber()          -- Per-file: apertures, X2 attributes, flash/draw counts, coord range
parse_drill()           -- Per-file: tool definitions, hole counts, coord range, PTH/NPTH type
scan_zip_archives()     -- Zip contents inventory + timestamp comparison vs loose files
    |
    v
identify_layer_type()   -- Map filename/X2 attributes to KiCad layer names (F.Cu, B.Mask, etc.)
check_completeness()    -- Verify required layers present (F.Cu, B.Cu, F.Mask, B.Mask, Edge.Cuts)
check_alignment()       -- Compare coordinate extents across copper/edge layers
    |
    v
JSON output
```

### Layer Identification

Uses two strategies, in order:
1. **X2 attributes**: <code>%TF.FileFunction,...*%</code> headers (modern gerbers from KiCad 6+)
2. **Filename patterns**: Maps common suffixes/extensions to layers (e.g., <code>.gtl</code> → F.Cu, <code>.gbl</code> → B.Cu, <code>F_Cu.gbr</code> → F.Cu)

**Pitfall**: The filename patterns dictionary is case-insensitive substring matching. Non-standard naming (e.g., a file called <code>top_copper.ger</code>) won't be identified. Add patterns to the <code>patterns</code> dict in <code>identify_layer_type()</code> as needed.

### Alignment Check

Compares bounding box extents across copper and edge layers. Only checks F.Cu, B.Cu, and Edge.Cuts — paste, silk, mask, and drill layers naturally have smaller extents. A >2mm difference flags an alignment issue.

### Drill File Parsing

- Handles both metric and inch formats (auto-detects from <code>METRIC</code>/<code>INCH</code> keywords)
- Inch values are converted to mm internally
- PTH vs NPTH is determined from filename (<code>-PTH.drl</code> vs <code>-NPTH.drl</code>)
- Individual hole coordinates are parsed for coordinate range but not included in output (too verbose)

### Usage

```bash
python3 analyze_gerbers.py ./gerbers/                    # JSON to stdout
python3 analyze_gerbers.py ./gerbers/ --output out.json  # JSON to file
python3 analyze_gerbers.py ./gerbers/ --compact          # Minified JSON
```

---

## analyze_schematic.py

The largest and most complex script. The rest of this document focuses on its architecture and pitfalls.

### Pipeline

```
.kicad_sch file(s)
    |
    v
parse_single_sheet()          -- S-expression parsing, component/wire/label extraction
    |
    v
analyze_schematic()           -- Multi-sheet orchestration, instance remapping
    |  Builds: all_components, all_wires, all_labels, all_junctions
    |
    v
build_net_map()               -- Union-find net building (sheet-aware coordinates)
    |  Produces: nets dict {name -> {pins, labels, ...}}
    |
    v
analyze_signal_paths()        -- Subcircuit detection (VD, RC, regulators, bridges, etc.)
analyze_design_rules()        -- Bus detection, diff pairs, power domains, ERC
analyze_ic_pinouts()          -- Per-IC pin connectivity summary
compute_statistics()          -- Counts, BOM dedup
    |
    v
Output harmonization           -- All detections → flat findings[] with rich envelopes
                                  (detector, rule_id, severity, confidence, recommendation)
                                  rail_voltages/net_classifications promoted to top level
    |
    v
JSON output                    -- {analyzer_type, summary, findings[], components, nets, ...}
```

### Key Data Structures

- **<code>nets</code>**: <code>{net_name: {"pins": [{component, pin_number, pin_name, pin_type, x, y}], ...}}</code>
- **<code>pin_net</code>**: <code>{(reference, pin_number): (net_name, pin_type)}</code> — reverse lookup from <code>build_pin_to_net_map()</code>
- **<code>comp_lookup</code>**: <code>{reference: component_dict}</code> — built locally in analysis functions
- **<code>parsed_values</code>**: <code>{reference: float}</code> — numeric values for passive components

## File Format Support

### Modern <code>.kicad_sch</code> (KiCad 6+)
Full support. S-expression format parsed by <code>sexp_parser.py</code>.

### Legacy <code>.sch</code> (KiCad 4/5)
Line-based format. Components, wires, labels, power symbols parsed. <code>.lib</code> symbol libraries are parsed for pin definitions (<code>parse_legacy_lib()</code>), enabling pin-to-net mapping via geometric snapping. Library resolution searches cache-lib, sym-lib-table, LIBS: directives, and built-in defaults. Pin geometry uses a snapping radius (up to 12mm) when parsed symbols are incomplete — results are heuristic and carry reduced confidence compared to KiCad 6+ native pin data.

### Eagle <code>.sch</code>
Not supported (binary and XML formats). Returns 0 components gracefully.

## Critical Concepts

### Sheet-Aware Coordinate Keys

**Problem**: Different hierarchical sheets can have wires at identical coordinates. Without sheet separation, the union-find merges nets across sheets (e.g., +3V3 and +5V merge because wires at (100,50) exist on both sheets).

**Solution**: Every element (component, wire, label, junction) is tagged with <code>_sheet</code> index. All coordinate-based keys in <code>build_net_map()</code> include the sheet index: <code>(x, y, sheet)</code> not <code>(x, y)</code>.

**Pitfall**: If you add new coordinate-based lookups, always include <code>_sheet</code> in the key. Forgetting this causes silent cross-sheet net merges that are extremely hard to debug.

### Multi-Instance Hierarchical Sheets

**Problem**: A parent sheet can reference the same sub-sheet file multiple times (e.g., 3 instances of <code>h_bridge.kicad_sch</code> for 3 motor phases). Each instance has different component references (Q1/Q2, Q3/Q4, Q5/Q6).

**How it works**:
1. <code>parse_single_sheet()</code> returns sub-sheet entries as <code>(path, uuid)</code> tuples
2. The main loop tracks <code>(file_path, instance_uuid)</code> pairs — same file with different UUIDs gets parsed separately
3. <code>extract_components()</code> reads the <code>(instances)</code> block in each symbol to remap the reference designator for the specific instance UUID
4. Each instance gets its own <code>_sheet</code> index

**KiCad storage format**: Each symbol in a sub-sheet has:
```
(instances
  (project "project_name"
    (path "/root_uuid/sheet_instance_uuid"
      (reference "Q4")
      (unit 1))
    (path "/root_uuid/other_instance_uuid"
      (reference "Q6")
      (unit 1))))
```

The sheet's UUID comes from the parent's <code>(sheet ... (uuid "xxx"))</code> block.

### Multi-Unit Symbols

**Problem**: ICs like STM32 have multiple units (GPIO unit, power unit, etc.) placed as separate symbols on the schematic. Each unit has different pins, but they share the same reference (e.g., U1).

**Solution**:
- <code>extract_lib_symbols()</code> stores pins per unit in <code>unit_pins</code> dict
- <code>extract_components()</code> reads <code>(unit N)</code> from each placed symbol
- <code>compute_pin_positions()</code> filters pins by unit number
- <code>generate_bom()</code> and <code>compute_statistics()</code> deduplicate by reference (count U1 once, not per unit)

**Pitfall**: Multi-unit components appear multiple times in <code>all_components</code>. Always use reference-based dedup when counting unique components.

### Label Scoping Rules

- **Local labels** (<code>label</code>): Connect only within their sheet (<code>_sheet</code> index must match)
- **Global labels** (<code>global_label</code>): Connect across all sheets
- **Hierarchical labels** (<code>hierarchical_label</code>): Connect via parent sheet's hierarchical pin
- **Power symbols**: Behave like global labels (connect across all sheets by name)

In <code>build_net_map()</code>, local labels use <code>(name, sheet)</code> as their union key, while global/hierarchical labels and power symbols use <code>(name,)</code> (no sheet).

### Net Name Assignment

Nets are assigned names with this priority:
1. Power symbol name (e.g., "GND", "+3V3")
2. Global/hierarchical label name
3. Local label name
4. <code>__unnamed_N</code> for nets with no label

**Duplicate name handling**: When multiple disconnected wire groups share the same net name (e.g., two separate "GND" connections via local labels), the second group's pins are merged into the first's net entry rather than overwriting it. This was a previous bug (commit f8ae22b).

## Value Parser

<code>parse_value()</code> converts component value strings to floats:

| Input | Output | Notes |
|-------|--------|-------|
| <code>"4.7k"</code> | 4700.0 | SI prefix |
| <code>"4K7"</code> | 4700.0 | Embedded multiplier |
| <code>"0R1"</code> | 0.1 | R as decimal point |
| <code>"100n"</code> | 1e-7 | |
| <code>"300µ"</code> | 0.0003 | Unicode micro |
| <code>"0.3mOhm"</code> | 0.0003 | Ohm suffix stripped |
| <code>"220k/R0402"</code> | 220000.0 | Splits on "/" first |
| <code>"4.7k 1%"</code> | 4700.0 | Tolerance stripped |
| <code>"DNP"</code> | None | Not parseable |

**Pitfall**: The parser is generous — it will parse the first numeric-looking thing it finds. Value fields like "FDMT80080DC" (a MOSFET part number) may parse to a number. Always check <code>c["type"]</code> before using parsed values.

## Signal Analysis Patterns

### Detection Pattern: Two Resistors Sharing a Net (Voltage Dividers)

Iterates all resistor pairs, finds shared nets (mid-point), checks endpoints for power/ground.

**Known pitfalls**:
- **R_top/R_bottom assignment**: After swapping r1/r2 to fix orientation, must re-derive net membership from current r1/r2 (not stale <code>r1_n1</code> variables). Previous bug: stale variables caused ratio inversion.
- **Power rail mid-point filter**: If the mid-point has >4 connections and is a power/ground net, reject — it's a bus, not a divider output.
- **Solder jumper gaps**: Dividers gated by solder jumpers (SJ) break the direct R-R series topology. Accepted limitation.

### Detection Pattern: IC Pin Matching (Regulators, Op-amps)

Scans IC pins by name (FB, SW, BOOT, VIN, VOUT, etc.) to classify function.

**Key rule**: Strip trailing digits before matching (<code>pn_base = pname.rstrip("0123456789")</code>). Multi-channel regulators have pins like FB1, SW2, ADJ2.

**Regulator false positive prevention**: ICs without FB/SW/BOOT pins require regulator keywords in lib_id/value. ICs with SW pin but no inductor on the SW net also require keywords. This prevents analog ICs with "SW" pins (like AD8233 gain switch) from being classified as regulators.

### Detection Pattern: Component on Both Sides (Current Sense, Bridges)

Finds ICs connected to both nets of a 2-terminal component (shunt resistor for current sense, transistors for bridges).

**4-pin Kelvin shunts**: Check for pin 3/4 presence *before* using <code>get_two_pin_nets()</code>. Kelvin shunts have pins 1,4 (current path) and pins 2,3 (sense). <code>get_two_pin_nets()</code> returns pins 1,2 which is wrong for Kelvin.

**1-hop tracing**: For current sense, if no IC is found directly on both sides of the shunt, trace through resistors (filter resistors between shunt and sense IC are common in BMS designs).

### Detection Pattern: Keyword Matching (ESD, Memory, RF)

Many detectors use keyword lists to identify component types from value/lib_id strings. When adding new keywords:
- Use lowercase matching (<code>val.lower()</code>)
- Test against the batch suite to check false positive rates
- Substring matching can be too broad (e.g., <code>"power"</code> matched <code>"dc-power-supply-rescue"</code> — fixed by requiring exact prefix match or <code>_power</code> suffix)

### Component Type Classification

<code>classify_component()</code> uses reference prefix → type mapping, then fallback keyword checks on value/lib_id.

**X prefix ambiguity**: X can mean crystal (IEC standard) or connector (some designers). The code checks value/lib keywords. Active oscillators (MEMS, TCXO) with "oscillator" in lib but not "crystal"/"xtal" get typed as <code>"oscillator"</code> (an IC-like active device), not <code>"crystal"</code> (passive).

**Power symbols**: Detected by <code>(power)</code> flag in lib_symbol definition, or <code>#PWR</code>/<code>#FLG</code> reference prefix, or <code>lib_prefix == "power"</code> / <code>lib_prefix.endswith("_power")</code>. The substring check was previously too broad.

## Adding New Detection Features

1. **Start with the net graph**. Most detections work by finding components sharing nets with specific topologies.

2. **Use <code>get_two_pin_nets()</code>** for passive 2-terminal components. For multi-pin ICs, iterate <code>pin_net.get((ref, pin_number))</code>.

3. **Filter high-fanout nets**. Power rails (+3V3, GND) connect to many components. Most detection patterns should skip or special-case nets with >4-6 connections, or nets identified as power/ground by <code>is_power_net()</code>/<code>is_ground()</code>.

4. **Test against the harness**. See <code>kicad-happy-testharness</code> repo — <code>run_tests.py --smoke</code> runs the 565-test PR-gate subset in ~30s with no corpus dependency, <code>run_tests.py --quick-sanity</code> runs 5-repo assertions, and <code>run/run_schematic.py --jobs 16</code> runs the full 5,829-repo corpus regression (~30 min).

5. **Count detections across the corpus** to calibrate sensitivity. Too many detections (>1000 for a specific pattern across the 36,000+ schematic files) suggests false positives. Too few (<5) might mean overly narrow keywords. Use <code>run/run_schematic.py --cross-section smoke</code> or <code>--cross-section quick_200</code> for faster calibration passes.

6. **Validate manually** against 2-3 known schematics where the pattern definitely exists. Check that component references, net names, and computed values match what you see in the raw schematic.

## Test Harness

Location: <code>kicad-happy-testharness</code> sibling repo.

- 5,829 open-source KiCad projects spanning KiCad 5 through 10
- ~36,500 schematic files, ~18,700 PCB files, ~5,500 gerber dirs
- 2M+ regression assertions at 99.98%+ pass, 565-test smoke subset, 5-repo quick-sanity
- Schema drift tests across all 8 analyzer types
- Equation audit (107 tagged equations), constants audit (105+ switching freqs), bugfix guards

The harness is the authoritative validation layer. For the legacy <code>batchtest</code> directory some older scripts reference — the 1,053-file subset that lived under <code>~/Projects/sandbox/batchtest/</code> — is retired. All new detector work validates against the harness corpus.

## Known Remaining Limitations

- **Legacy pin mapping**: <code>.sch</code> pin-to-net mapping uses heuristic geometry snapping (up to 12mm radius) when <code>.lib</code> symbols are incomplete or resolved from fallback sources
- **Vout estimation**: Feedback divider Vout uses hardcoded Vref guesses (0.6, 0.8, 1.0, 1.22, 1.25V) without a component database
- **Regulator output_rail**: Switching regulators sometimes show null output_rail when the power net is on the inductor output side
- **Eagle files**: Not parseable — output 0 components
