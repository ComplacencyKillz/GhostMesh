# Schematic Analyzer — Methodology

This document describes the analysis methodology used by <code>analyze_schematic.py</code> and its supporting modules. It covers the parsing pipeline, net-building algorithm, component classification heuristics, signal path detection, and design analysis checks in enough detail to understand what the analyzer does, why, and where it makes trade-offs.

## Design Philosophy

The analyzer is a **data extraction layer**, not a design rule checker. It outputs structured JSON containing neutral observations about circuit topology, component relationships, and design patterns. An LLM (or human reviewer) consumes this JSON alongside component datasheets to perform higher-level design review.

This means the analyzer:
- Reports what it finds, not what it thinks is wrong
- Avoids opinionated warnings in favor of factual observations
- Prioritizes completeness (extract everything) over filtering (show only problems)
- Casts a wide net for detection, but reported facts must be accurate — an undetected circuit is a blind spot for the reviewer; an incorrectly reported one is misinformation. Both are costly when PCB orders are at stake, so detectors favor thorough matching while keeping the reported values, connections, and classifications precise

## Architecture Overview

```
.kicad_sch file(s)
       |
       v
  sexp_parser.py          Parse S-expressions into nested Python lists
       |
       v
  analyze_schematic.py     Multi-sheet recursive descent, extraction, analysis
   |       |       |
   v       v       v
kicad_utils.py  kicad_types.py  signal_detectors.py
(classification,  (AnalysisContext    (21 circuit pattern
 value parsing,    shared state)       detectors)
 net name rules)
       |
       v
  Structured JSON output
```

### Module Responsibilities

| Module | Role |
|--------|------|
| <code>sexp_parser.py</code> | Generic S-expression tokenizer and parser. No KiCad-specific knowledge. |
| <code>kicad_utils.py</code> | Component classification, engineering value parsing, power/ground net name detection. Stateless utility functions. |
| <code>kicad_types.py</code> | <code>AnalysisContext</code> dataclass — shared state built once and passed to all analysis functions. Holds components, nets, pin-net map, pre-computed lookups. |
| <code>signal_detectors.py</code> | 21 detector functions + 2 shared helpers. Each identifies a specific circuit pattern (voltage dividers, RC filters, regulators, etc.) from the connectivity graph. |
| <code>analyze_schematic.py</code> | Orchestrator. Handles file parsing, multi-sheet traversal, net building, BOM generation, and all analysis functions not in signal_detectors. Assembles the final JSON output. |

---

## 1. S-Expression Parsing

KiCad stores schematics in a Lisp-like S-expression format:

```lisp
(kicad_sch (version 20231120) (generator "eeschema")
  (symbol (lib_id "Device:R") (at 100 50 0)
    (property "Reference" "R1" ...)
    (property "Value" "10k" ...)
    (pin "1" (at 0 -1.27) ...)
    (pin "2" (at 0 1.27) ...)))
```

The parser (<code>sexp_parser.py</code>) converts this to nested Python lists:

```python
["kicad_sch", ["version", "20231120"], ["generator", "eeschema"],
  ["symbol", ["lib_id", "Device:R"], ["at", "100", "50", "0"],
    ["property", "Reference", "R1", ...],
    ["property", "Value", "10k", ...], ...]]
```

**Key design decision**: All values are strings. The parser performs no type coercion, no schema validation, and no KiCad version-specific handling. This makes it robust across KiCad 5–10 format changes. Callers convert to <code>float</code>/<code>int</code> as needed.

Helper functions (<code>find_all</code>, <code>find_first</code>, <code>find_deep</code>, <code>get_value</code>, <code>get_property</code>) provide structured traversal of the parse tree without requiring callers to write index-based list access.

---

## 2. Multi-Sheet Parsing

KiCad schematics can span multiple <code>.kicad_sch</code> files in a hierarchy. The root sheet references sub-sheets via <code>(sheet ...)</code> blocks, each with a UUID.

### Traversal

The analyzer uses breadth-first traversal starting from the root sheet:

1. Parse the root file. Extract <code>(sheet ...)</code> blocks to find sub-sheet file paths and instance UUIDs.
2. For each sub-sheet, queue <code>(file_path, instance_path)</code> pairs.
3. Parse each sheet file once per instance. A single <code>.kicad_sch</code> file may be instantiated multiple times (e.g., three identical half-bridge phases) — each gets its own instance UUID and reference remapping.
4. Track <code>(file_path, instance_path)</code> pairs to prevent re-parsing the same instance.

### Multi-Instance Support

When a sub-sheet is instantiated multiple times, each symbol in it has per-instance reference assignments:

```lisp
(instances
  (project "proj"
    (path "/root_uuid/instance1_uuid" (reference "Q1") (unit 1))
    (path "/root_uuid/instance2_uuid" (reference "Q3") (unit 1))))
```

The analyzer matches the current instance path to extract the correct reference designator. Some KiCad projects (especially migrated ones) store these mappings only in the root schematic's centralized <code>(symbol_instances)</code> section rather than inline — both locations are checked as fallback.

### Sheet Isolation

Every extracted element (component, wire, label, junction) is tagged with a <code>_sheet</code> index. This prevents coordinate-based net building from merging unrelated elements that happen to share the same (x, y) position on different sheets.

---

## 3. Component Extraction

Each <code>(symbol ...)</code> block in the parse tree becomes a component dict:

```python
{
    "reference": "R1",
    "value": "10k",
    "lib_id": "Device:R",
    "footprint": "Resistor_SMD:R_0402_1005Metric",
    "x": 100.0, "y": 50.0, "angle": 0.0,
    "type": "resistor",           # from classify_component()
    "pins": [...],                 # computed absolute positions
    "properties": {...},           # all KiCad properties
    "dnp": False,                  # Do Not Populate flag
    "_sheet": 0                    # sheet index for net isolation
}
```

### Pin Position Computation

Library symbols define pin positions relative to the symbol origin. The analyzer transforms these to absolute schematic coordinates:

1. Look up the component's <code>lib_id</code> in the library symbol table.
2. For multi-unit symbols, merge unit-specific pins with unit 0 (shared pins — typically power/ground).
3. Apply mirror transforms (KiCad's <code>(mirror x)</code> or <code>(mirror y)</code>).
4. Apply rotation (symbol placement angle).
5. Apply Y-axis inversion (symbol coordinates use math-up convention; schematic coordinates use screen-down).
6. Add the component's placement offset <code>(at x y)</code>.

This gives each pin an absolute <code>(x, y)</code> in schematic space, which is how pins connect to wires in the net-building step.

### Legacy Format Support

KiCad 4/5 <code>.sch</code> files use a line-based format with <code>$Comp</code>/<code>$EndComp</code> blocks, <code>Wire Wire Line</code> entries, and <code>Text Label</code>/<code>Text GLabel</code> markers. The analyzer has a separate parser for this format that:

- Converts coordinates from mils (1/1000 inch) to mm
- Parses the 2x2 orientation matrix to extract rotation angle and mirror flags
- Extracts labels, wires, junctions, and no-connects with their own syntax
- Parses <code>.lib</code> files (cache libraries and project libs) to populate pin positions and types
- Computes absolute pin positions using <code>compute_pin_positions()</code> (same transform as KiCad 6+)
- Performs subcircuit detection via <code>identify_subcircuits()</code>

The <code>.lib</code> resolution strategy: (1) prefer <code>*-cache.lib</code> alongside the root <code>.sch</code> (self-contained), (2) fall back to individual <code>LIBS:</code> directives searching the project directory tree, (3) use built-in defaults for common standard library symbols (R, C, L, D, LED, transistors). Coverage is typically 92–100% depending on which <code>.lib</code> files are available in the repo.

---

## 4. Net Building (Union-Find)

The net-building algorithm is the core of the analyzer. It determines which pins are electrically connected by tracing wires, labels, junctions, and power symbols.

### Algorithm: Union-Find on Sheet-Aware Coordinates

Every electrical point in the schematic is assigned a coordinate key:

```python
key = (sheet_index, round(x / EPSILON) * EPSILON, round(y / EPSILON) * EPSILON)
```

where <code>EPSILON = 0.01 mm</code>. The sheet index prevents cross-sheet merges; the rounding absorbs floating-point noise in coordinates.

A standard union-find (disjoint set) data structure with path compression groups connected points into equivalence classes:

```
find(p):  path compression to root
union(a, b):  merge two sets
```

### Connection Sources (in processing order)

1. **Component pins** — Each pin's absolute position becomes a point. PWR_FLAG symbols are excluded (they are ERC-only markers with no real electrical connection).

2. **Wire endpoints** — Each wire's <code>(x1, y1)</code> and <code>(x2, y2)</code> are added as points and unioned together. Wires are also indexed in a spatial grid (5mm cells) for fast mid-wire point lookup.

3. **Labels** — Added at their placement position. If a label sits mid-wire (not at an endpoint), the <code>point_on_segment()</code> function detects this and unions the label with the wire.
   - **Local labels**: Only connect within the same sheet. Keyed by <code>(name, sheet)</code>.
   - **Global labels**: Connect across all sheets. Keyed by <code>(name,)</code>.
   - **Hierarchical labels**: Connect across sheets (parent ↔ child). Keyed by <code>(name,)</code>.

4. **Power symbols** — Always cross-sheet. The symbol's pin position (not its center) is used as the connection point. PWR_FLAG symbols are excluded.

5. **Junctions** — Added and unioned with overlapping wires.

6. **Mid-wire pin placement** — A second pass checks if any component pin lands on a wire segment between its endpoints (rare but legal in KiCad).

### Mid-Wire Detection

Points can be placed anywhere on a wire, not just at endpoints. The analyzer detects this with:

1. **Bounding box check**: Quick rejection if the point is outside the wire's bounding box (± 0.05mm tolerance).
2. **Cross product collinearity**: The perpendicular distance from point to wire line must be < 0.05mm.

A spatial grid index (5mm cells) makes this efficient — only wire segments in the same grid cell are tested, avoiding O(W×P) full scans.

### Net Naming Priority

After union-find, connected groups are named by priority:
1. **Power symbol name** (e.g., <code>+3V3</code>, <code>GND</code>) — highest priority
2. **Label name** (global, hierarchical, or local)
3. **<code>__unnamed_N</code>** — auto-generated for nets with pins but no labels

When multiple disconnected wire groups share the same name (e.g., two separate <code>GND</code> networks), their pins are merged into a single net entry. This matches KiCad's behavior where same-named labels create global connectivity.

### Output

```python
nets = {
    "net_name": {
        "name": "net_name",
        "pins": [
            {"component": "R1", "pin_number": "1", "pin_name": "~", "pin_type": "passive"},
            {"component": "U1", "pin_number": "14", "pin_name": "VCC", "pin_type": "power_in"},
        ],
        "point_count": 7   # total coordinate points in this net (wires + junctions + labels + pins)
    }
}
```

---

## 5. Component Classification

<code>classify_component()</code> in <code>kicad_utils.py</code> assigns a type string to each component based on its reference designator, library ID, and value field.

### Classification Hierarchy

The classifier applies rules in priority order — first match wins:

1. **Power flag check**: <code>is_power</code> flag set, or <code>lib_id</code> starts with <code>"power:"</code> → <code>"power_symbol"</code>

2. **Reference prefix lookup**: The reference designator prefix (letters before digits) is matched against a type map:

   | Prefix | Type | Notes |
   |--------|------|-------|
   | <code>R</code>, <code>RS</code> | <code>resistor</code> | Standard resistors, shunts |
   | <code>RN</code>, <code>RM</code>, <code>RA</code> | <code>resistor_network</code> | Arrays and networks |
   | <code>C</code> | <code>capacitor</code> | |
   | <code>L</code> | <code>inductor</code> | |
   | <code>D</code> | <code>diode</code> | Unless overridden by LED check |
   | <code>Q</code>, <code>FET</code> | <code>transistor</code> | BJTs and FETs |
   | <code>U</code>, <code>IC</code> | <code>ic</code> | Integrated circuits |
   | <code>J</code>, <code>P</code> | <code>connector</code> | Jacks, plugs, headers |
   | <code>SW</code>, <code>S</code>, <code>BUT</code> | <code>switch</code> | |
   | <code>Y</code> | <code>crystal</code> | IEC standard prefix |
   | <code>F</code>, <code>FUSE</code> | <code>fuse</code> | |
   | <code>K</code> | <code>relay</code> | |
   | <code>OK</code>, <code>OC</code> | <code>optocoupler</code> | |
   | <code>LED</code> | <code>led</code> | |
   | <code>FB</code> | <code>ferrite_bead</code> | |
   | <code>TP</code> | <code>test_point</code> | |
   | <code>MH</code>, <code>H</code> | <code>mounting_hole</code> | |
   | <code>JP</code>, <code>SJ</code> | <code>jumper</code> | Solder jumpers |
   | <code>#PWR</code> | <code>power_flag</code> | |
   | <code>#FLG</code> | <code>flag</code> | |

3. **Library/value keyword overrides**: Certain combinations override the prefix-based type:
   - <code>D</code> prefix + <code>"led"</code> in lib → <code>"led"</code> (not diode)
   - <code>R</code> prefix + <code>"potentiometer"</code> in lib → <code>"resistor"</code> (not varistor)
   - <code>T</code> prefix + <code>"mosfet"</code>/<code>"transistor"</code> in lib → <code>"transistor"</code> (not transformer)
   - Thermistor + <code>"fuse"</code>/<code>"polyfuse"</code> in lib → <code>"fuse"</code>

4. **X prefix special handling** (crystal vs. oscillator vs. connector):
   - Contains <code>"oscillator"</code> but NOT <code>"crystal"</code>/<code>"xtal"</code> → <code>"oscillator"</code> (active IC)
   - Contains known active oscillator part numbers (DSC6, Si5, SiT8, etc.) → <code>"oscillator"</code>
   - Contains <code>"xtal"</code>/<code>"crystal"</code>/<code>"mhz"</code>/<code>"khz"</code> → <code>"crystal"</code> (passive)
   - Otherwise → <code>"connector"</code>

5. **Library-based fallback**: For non-standard prefixes, keyword search through <code>lib_id</code> and <code>value</code> for terms like <code>thermistor</code>, <code>varistor</code>, <code>optocoupler</code>, <code>jumper</code>, <code>connector</code>, <code>switch</code>, <code>relay</code>, <code>net_tie</code>, <code>transistor</code>, <code>diode</code>, <code>fuse</code>, <code>inductor</code>, <code>capacitor</code>, <code>resistor</code>.

6. **Non-standard connector prefixes**: <code>CON</code>, <code>USB</code>, <code>MICROSD</code>, <code>UEXT</code>, <code>LAN</code>, <code>HDMI</code>, <code>EXT</code>, <code>GPIO</code>, <code>CAN</code>, <code>SWD</code>, <code>JTAG</code>, <code>ANT</code>, <code>RJ</code>, <code>SUPPLY</code> → <code>"connector"</code>

7. **Default**: <code>"other"</code>

### Type Taxonomy

The full set of possible type values:

```
resistor, resistor_network, capacitor, inductor, diode, led, transistor,
ic, connector, switch, crystal, oscillator, fuse, relay, optocoupler,
ferrite_bead, test_point, mounting_hole, jumper, net_tie, transformer,
thermistor, varistor, buzzer, motor, antenna, power_symbol, power_flag,
flag, other
```

---

## 6. Value Parsing

<code>parse_value()</code> converts component values in engineering notation to numeric floats.

### Supported Formats

| Input | Output | Rule |
|-------|--------|------|
| <code>10k</code> | 10000.0 | SI suffix: k = ×10³ |
| <code>4K7</code> | 4700.0 | Embedded multiplier (K as decimal point): 4.7 × 10³ |
| <code>0R1</code> | 0.1 | R as decimal point: 0.1 Ω |
| <code>100n</code> | 1e-7 | SI suffix: n = ×10⁻⁹ |
| <code>4.7u</code> | 4.7e-6 | SI suffix: u/µ = ×10⁻⁶ |
| <code>2.2pF</code> | 2.2e-12 | SI suffix: p = ×10⁻¹² (unit stripped) |
| <code>100</code> | 100.0 | Plain number |
| <code>1M</code> | 1e6 | SI suffix: M = ×10⁶ |
| <code>22G</code> | 2.2e10 | SI suffix: G = ×10⁹ |

### Pre-Processing

Before parsing, the function:
- Strips leading/trailing whitespace
- Removes unit suffixes: <code>Ω</code>, <code>ohm</code>, <code>F</code>, <code>H</code>, <code>Hz</code> (case-insensitive)
- Removes tolerance specs: <code>1%</code>, <code>5%</code>
- Removes package descriptors: <code>/R0402</code>, <code>/0603</code>
- Removes voltage ratings embedded after separators

### SI Prefix Map

```
p = 1e-12    n = 1e-9     u/µ = 1e-6    m = 1e-3
k/K = 1e3    M = 1e6      G = 1e9
```

### Unparseable Values

Returns <code>None</code> for:
- Part numbers: <code>"FDMT80080DC"</code>, <code>"STM32F407VGT6"</code>
- DNP markers: <code>"DNP"</code>, <code>"do not place"</code>
- Non-numeric strings: <code>"NTC"</code>, <code>"PTC"</code>, <code>"ANTENNA"</code>
- Empty strings

**Important caveat**: The parser is generous — it extracts the first numeric-looking substring. Always check component type before using the parsed value (e.g., a transistor's <code>"2N3904"</code> might parse as <code>2.0</code> if not guarded).

---

## 7. Power Net and Ground Detection

### Power Rail Identification

A net is classified as a power rail by two complementary methods:

**Method 1 — Power symbol registration**: Any net that has a pin from a <code>#PWR</code> or <code>#FLG</code> component is added to the <code>known_power_rails</code> set. This is authoritative — it captures whatever the designer connected via power symbols.

**Method 2 — Name heuristics** (<code>is_power_net_name()</code>): Checks the net name against common power naming conventions:

- **Exact matches**: <code>VCC</code>, <code>VDD</code>, <code>AVCC</code>, <code>AVDD</code>, <code>DVCC</code>, <code>DVDD</code>, <code>VBUS</code>, <code>VIO</code>, <code>VMAIN</code>, <code>VPWR</code>, <code>VSYS</code>, <code>VBAT</code>, <code>VCORE</code>, <code>VIN</code>, <code>VOUT</code>, <code>VREG</code>
- **Patterns**: Names starting with <code>+</code> (e.g., <code>+3V3</code>, <code>+5V</code>, <code>+12V</code>), <code>V</code> followed by digits (e.g., <code>V3V3</code>, <code>V1V8</code>)
- **Prefixes**: <code>VDD_*</code>, <code>VCC_*</code>, <code>VBAT_*</code>, <code>VSYS_*</code>, <code>VBUS_*</code>, etc.

A net is considered a power net if **either** method matches.

### Ground Net Identification

<code>is_ground_name()</code> checks:
- **Exact matches**: <code>GND</code>, <code>VSS</code>, <code>AGND</code>, <code>DGND</code>, <code>PGND</code>, <code>GNDPWR</code>, <code>GNDA</code>, <code>GNDD</code>
- **Patterns**: Starts or ends with <code>GND</code>, starts with <code>VSS</code>

---

## 8. AnalysisContext

<code>AnalysisContext</code> (in <code>kicad_types.py</code>) is a dataclass that holds shared state built once and passed to all analysis functions:

```python
@dataclass
class AnalysisContext:
    components: list[dict]          # All components across all sheets
    nets: dict[str, dict]           # Net connectivity map
    lib_symbols: dict               # Library symbol definitions
    pin_net: dict                   # (ref, pin_num) → (net_name, pin_name) map
    comp_lookup: dict[str, dict]    # {reference: component} — built in __post_init__
    parsed_values: dict[str, float] # {reference: numeric_value} — built in __post_init__
    known_power_rails: set[str]     # Power rail names — built in __post_init__
    generator_version: str          # KiCad generator version string
```

The three auto-built fields (<code>comp_lookup</code>, <code>parsed_values</code>, <code>known_power_rails</code>) replace what was previously ~10 duplicate rebuild operations scattered across analysis functions.

---

## 9. Signal Path Detection

The <code>analyze_signal_paths()</code> function orchestrates 21 detector functions that identify common analog and mixed-signal circuit patterns from the connectivity graph. Each detector is a pure function that takes <code>AnalysisContext</code> (and optionally prior detector results) and returns structured detection results.

### Execution Order and Dependencies

Detectors run in a specific order because some consume results from earlier detectors:

```
1. voltage_dividers          ← standalone
2. rc_filters                ← excludes resistors in voltage dividers
3. lc_filters                ← standalone
4. crystal_circuits          ← standalone
5. decoupling                ← standalone
6. current_sense             ← standalone
7. power_regulators          ← matches feedback dividers from (1)
8. protection_devices        ← standalone
9. opamp_circuits            ← standalone
10. bridge_circuits          ← standalone
11. transistor_circuits      ← excludes bridge FETs from (10)
12. postfilter_vd_and_dedup  ← modifies (1) using (11)
13. led_drivers              ← enriches (11)
14. buzzer_speakers          ← standalone
15. key_matrices             ← standalone
16. isolation_barriers       ← standalone
17. ethernet_interfaces      ← standalone
18. memory_interfaces        ← standalone
19. rf_chains                ← standalone
20. bms_systems              ← standalone
21. design_observations      ← reads all prior results
```

### Shared Helpers

Two helper functions are used by multiple detectors:

- <code>_get_net_components(ctx, net_name, exclude_ref)</code> — Returns all components connected to a net, optionally excluding one reference. Used to find what else connects to a node.
- <code>_classify_load(ctx, net_name, exclude_ref)</code> — Classifies the load on a net by examining connected component types and net name keywords. Returns labels like <code>"motor"</code>, <code>"relay"</code>, <code>"led"</code>, <code>"speaker"</code>, <code>"resistive"</code>, <code>"capacitive"</code>, etc.

### Detector Details

#### 9.1 Voltage Dividers

**Pattern**: Two resistors sharing a mid-point net, with the other ends on power/ground.

**Algorithm**:
1. Index all resistors by their two nets (from <code>get_two_pin_nets()</code>).
2. For each net, find resistor pairs that share it as a common node.
3. For each pair, check if the non-shared nets are power-on-one-end and ground-or-power-on-the-other.
4. Calculate voltage divider ratio: <code>R_bottom / (R_top + R_bottom)</code>.

**Rejection filters**:
- Mid-point net has >4 pins and is a named power rail → bus, not divider output
- Either resistor is 0Ω → wire jumper, not divider
- Both non-shared nets are ground → not meaningful

**Feedback network separation**: If the mid-point net connects to an IC pin named <code>"FB"</code>, <code>"ADJ"</code>, or <code>"COMP"</code>, the divider is tagged as a feedback network and separated from generic voltage dividers.

#### 9.2 RC Filters

**Pattern**: Resistor and capacitor sharing exactly one non-power signal net.

**Algorithm**:
1. For each resistor, get its two nets.
2. For each capacitor, get its two nets.
3. Find R-C pairs sharing exactly one net that is NOT a power rail or ground.
4. Classify filter type based on grounding:
   - Capacitor's other pin to ground → **low-pass** (most common: decoupling + series R)
   - Resistor's other pin to ground → **high-pass**
   - Neither to ground → **RC-network** (coupling, etc.)

**Rejection filters**:
- Shared net only is ground → would match every R near every C
- Resistor already in a voltage divider → false positive (VD output + cap)
- Shared net has >6 pins → bus, not filter node
- R < 10Ω → likely series termination, not filter

**Cutoff frequency**: <code>f = 1 / (2π × R × C)</code>

**Parallel cap merging**: When the same resistor pairs with multiple capacitors on the same shared net, they are combined into one entry with summed capacitance.

#### 9.3 LC Filters

**Pattern**: Inductor and capacitor sharing one non-power net.

Same approach as RC filters but for L-C pairs. Computes resonant frequency: <code>f = 1 / (2π × √(L × C))</code> and characteristic impedance: <code>Z = √(L / C)</code>.

#### 9.4 Crystal Circuits

**Pattern**: Crystal with load capacitors.

1. Find all crystals (type <code>"crystal"</code>).
2. For each crystal, identify its two signal nets (non-power).
3. On each signal net, find capacitors with their other end to ground — these are load caps.
4. Compute effective load capacitance: <code>CL_eff = (C1 × C2) / (C1 + C2) + C_stray</code> where <code>C_stray ≈ 3pF</code>.

#### 9.5 Decoupling Analysis

**Pattern**: Capacitors directly between a power rail and ground.

1. Iterate all nets classified as power rails (not ground, not unnamed).
2. For each rail, find capacitors with one pin on the rail and the other on ground.
3. Group by rail. Sum total capacitance per rail.
4. Estimate self-resonant frequency per cap: <code>f_SRF = 1 / (2π × √(ESL × C))</code> where <code>ESL ≈ 1nH</code> (typical for SMD caps).

#### 9.6 Current Sense

**Pattern**: Low-value shunt resistor with sense IC.

1. Find resistors with value ≤ 0.5Ω and > 0Ω.
2. For each shunt, check both nets for connected ICs.
3. Support both 2-pin and 4-pin Kelvin sense configurations:
   - 2-pin: pins 1, 2 are the current path (sense tapped from same nets)
   - 4-pin: pins 1, 4 are current path; pins 2, 3 are Kelvin sense
4. Accept only if an IC connects to both sides of the shunt (directly or through a 1-hop filter resistor).
5. Reject if both nets are ground (bulk decoupling, not current sense).

**Calculated values**: Max current at 50mV and 100mV drop: <code>I = V / R_shunt</code>.

#### 9.7 Power Regulators

**Pattern**: IC with power conversion pins identified by name.

**Pin name scanning**: Each IC's pins are checked for keywords indicating a regulator:
- <code>FB</code>, <code>ADJ</code>, <code>COMP</code> — feedback/compensation
- <code>SW</code>, <code>BOOT</code>, <code>BST</code> — switching node / bootstrap
- <code>VIN</code>, <code>VOUT</code> — input/output power
- <code>EN</code>, <code>ENABLE</code>, <code>SHDN</code> — enable/shutdown
- <code>PG</code>, <code>PGOOD</code> — power good output
- <code>SS</code>, <code>SOFTSTART</code> — soft-start

Pin names are normalized (trailing digits stripped: <code>FB1</code> → <code>FB</code>, <code>SW2</code> → <code>SW</code>).

**Regulator type classification**:
- **LDO**: Has FB pin + ≥2 power pins, no SW/BOOT pins
- **Switching**: Has SW + BOOT pin, or SW + inductor on output net
- **Charge pump**: Has BOOST pin, no FB
- **Generic**: Has regulation pins but doesn't match a specific topology

**False positive prevention**: An IC with <code>SW</code> but no inductor on the switch net and no regulator keywords in its lib_id/value is NOT classified as a switching regulator. This prevents motor driver ICs or other switchers from being misidentified.

**Output voltage estimation**:
1. Look up the part number in <code>_REGULATOR_VREF</code> — a 150+ entry table of known regulators and their reference voltages.
2. If a feedback divider connects to the IC's FB pin, compute: <code>Vout = Vref × (1 + R_top / R_bottom)</code>.
3. If no feedback divider, check if the net name encodes a voltage (e.g., <code>+3V3</code> → 3.3V).

#### 9.8 Protection Devices

**Pattern**: Components providing ESD, overvoltage, overcurrent, or surge protection.

Identifies:
- **TVS diodes**: Diode type with <code>"tvs"</code>, <code>"esd"</code>, <code>"transient"</code> in lib_id/value
- **Varistors/MOVs**: Varistor type or <code>V</code> prefix
- **Polyfuses/PTCs**: Thermistor type with <code>"fuse"</code>, <code>"polyfuse"</code>, <code>"pptc"</code> keywords
- **ESD protection ICs**: IC type with ESD-related keywords (TI TPD series, ON Semi, etc.)
- **Series termination resistors**: R ≤ 100Ω on high-speed nets (USB, CAN, Ethernet)

#### 9.9 Op-Amp Circuits

**Pattern**: IC identified as op-amp/comparator with feedback network.

1. Identify op-amps by keywords in lib_id: <code>"opamp"</code>, <code>"op-amp"</code>, <code>"comparator"</code>, <code>"OPA"</code>, <code>"LM358"</code>, <code>"TL07"</code>, etc.
2. Find inverting and non-inverting input pins.
3. Trace feedback path from output to input.
4. Classify configuration:
   - Resistive feedback from output to inverting input → **inverting amplifier** or **non-inverting amplifier**
   - Capacitor in feedback → **integrator** or **differentiator**
   - No feedback → **comparator**
5. Calculate gain from feedback resistor ratio: <code>G = -Rf/Rin</code> (inverting) or <code>G = 1 + Rf/Rin</code> (non-inverting).

#### 9.10 Bridge Circuits

**Pattern**: 4 matched transistors in H-bridge or half-bridge topology.

1. Find transistor pairs sharing the same net (potential bridge leg).
2. Look for two legs sharing supply and return nets.
3. Identify gate/base drive signals.
4. Classify topology: full H-bridge, half-bridge, boost, or buck based on configuration.

#### 9.11 Transistor Circuits

**Pattern**: Single transistor (not part of a bridge) acting as switch or amplifier.

1. Skip transistors already matched as bridge components.
2. Identify collector/drain, emitter/source, base/gate nets.
3. Classify load type by connected components and net name keywords.
4. Detect biasing: base/gate resistor, emitter/source degeneration.
5. Classify circuit: switching (motor, relay, solenoid), LED driver, amplifier, level shifter, etc.

#### 9.12 Post-Filter and Deduplication

Cleans up voltage divider and feedback network results:
- Removes voltage dividers that are actually feedback networks (already in feedback list)
- Filters dividers whose mid-point connects to a transistor gate/base (likely bias network, not standalone divider)
- Deduplicates entries that were detected from multiple net perspectives

#### 9.13 LED Drivers

Post-processes transistor circuits to add LED-specific information when <code>load_type == "led"</code>:
- PWM control signal identification
- Multi-LED configurations
- Current limiting resistor value

#### 9.14 Buzzer/Speaker Circuits

Identifies transistors driving buzzers or speakers:
- Net name keywords: <code>BUZZ</code>, <code>SPK</code>, <code>SPEAK</code>, <code>BEEP</code>
- Connected component type matching
- Identifies frequency generation and power supply

#### 9.15 Key Matrices

Detects keyboard matrix input arrangements:
- Resistor networks (RN type) or diode arrays in row/column patterns
- Cross-references with MCU GPIO pins for matrix dimension inference

#### 9.16 Isolation Barriers

Identifies galvanic isolation components:
- **Optocouplers**: OK/OC reference prefix or keywords
- **Transformers**: TR reference or transformer type
- **Digital isolators**: IC with <code>"isolator"</code>, <code>"galvanic"</code>, <code>"iso"</code> keywords
- Verifies that the component connects to different power domains (not single-domain)

#### 9.17 Ethernet Interfaces

Detects Ethernet interface chains:
- RJ45 connector (J prefix with <code>"RJ45"</code>/<code>"LAN"</code> in value)
- Magnetics (transformer or inductor on data pins)
- PHY IC identification by known part numbers
- Differential pair and termination checks

#### 9.18 Memory Interfaces

Identifies memory ICs and their bus connections:
- Address, data, and control bus topology
- Known memory types by keywords: SRAM, DRAM, Flash, EEPROM
- Pull-up/pull-down resistors on open-drain pins (e.g., I2C EEPROM)

#### 9.19 RF Chains

Detects RF amplifier chains:
- RF amplifier keywords: <code>"rf_amp"</code>, <code>"mmic"</code>, <code>"lna"</code>, <code>"pa"</code>
- Input/output matching networks (L-C combinations)
- Bias networks and stability compensation

#### 9.20 BMS (Battery Management Systems)

Identifies battery management circuits:
- BMS IC keywords: <code>"bq76"</code>, <code>"MAX17"</code>, etc.
- Cross-references current sense results (shunt resistors)
- Cell tap voltage divider networks
- Balancing topology detection (active vs. passive)

#### 9.21 Design Observations

Meta-analysis that reads all prior detector results and generates high-level observations:
- Unusual patterns (e.g., multiple regulators with identical output voltage)
- Missing support components (e.g., regulator without compensation network)
- Statistical summaries across all detected circuits

---

## 10. Design Analysis

Beyond signal path detection, the analyzer performs several categories of design analysis.

### 10.1 Connectivity Analysis

- **Unconnected pins**: Pins not on any net and not marked with a no-connect symbol
- **Single-pin nets**: Nets with only one connected pin (likely unfinished wiring)
- **Multi-driver nets**: Multiple output-type pins driving the same net
- **Power net summary**: Per-rail listing of connected components (for power budget analysis)

### 10.2 Design Rule Analysis

**Net Classification**: Every net is tagged with a functional class based on its name:

| Class | Keywords/Patterns |
|-------|-------------------|
| <code>ground</code> | GND, VSS, AGND, DGND |
| <code>power</code> | VCC, VDD, +3V3, VBUS, etc. |
| <code>clock</code> | SCL, SCK, CLK, MCLK, XTAL, OSC |
| <code>data</code> | SDA, MOSI, MISO, UART, TX, RX |
| <code>high_speed</code> | USB, CAN, LVDS, ETH |
| <code>analog</code> | ADC, AIN, VREF, VSENSE |
| <code>control</code> | RESET, NRST, EN, ENABLE |
| <code>chip_select</code> | CS, SS, NSS, CE, SEL |
| <code>interrupt</code> | INT, IRQ, ALERT, DRDY |
| <code>debug</code> | SWD, SWCLK, JTAG, TCK, TMS |
| <code>config</code> | BOOT, TEST |
| <code>signal</code> | Everything else |

**Power Domain Mapping**: For each IC, determines which power rails it connects to. Distinguishes IO-level pins (<code>VDDIO</code>, <code>VIO</code>, <code>VCCA</code>, <code>VCCB</code>) from internal supplies (<code>VCC</code>, <code>VDD</code>) — the IO rail determines signal levels for cross-domain analysis.

**Cross-Domain Signal Detection**: Identifies signals that cross between ICs powered by different voltage rails. Filters out signals that pass through level translators.

**Bus Protocol Analysis**:
- **I2C**: Checks for pull-up resistors on SDA/SCL lines, verifies pull-up to VCC
- **SPI**: Maps chip select assignments (CS per device)
- **UART**: Pairs TX/RX lines

**Differential Pair Analysis**: Identifies USB D+/D-, CAN H/L, LVDS pairs. Checks for proper termination resistors.

**ERC-Like Checks**:
- Input-to-input conflicts (net with only input pins, no driver)
- Output-to-output shorts (multiple output pins on same net)
- Undriven inputs (input pin on a net with no output or bidirectional pin)

### 10.3 Annotation Completeness

- Duplicate reference designators
- Unannotated references (containing <code>?</code>)
- Missing reference designators

### 10.4 Label Shape Validation

Checks for label type mismatches:
- Global labels used for local connections (only on one sheet)
- Local labels that appear to need global scope (same name on multiple sheets but not connected)

### 10.5 PWR_FLAG Audit

Identifies power nets that lack a PWR_FLAG symbol. KiCad's ERC requires PWR_FLAG on nets driven only by power pins (e.g., a connector providing power) — without it, ERC reports "power pin not driven."

### 10.6 Footprint Filter Validation

Checks whether assigned footprints match the library symbol's <code>ki_fp_filters</code> patterns. Reports mismatches that may indicate wrong footprint selection.

### 10.7 Sourcing Audit

Checks component properties for manufacturing readiness:
- Missing MPN (Manufacturer Part Number)
- Missing footprint
- Components without vendor/supplier fields
- DNP (Do Not Populate) markers

### 10.8 Ground Domain Classification

Groups ground nets into domains (analog ground, digital ground, power ground, chassis ground) and maps which components connect to each domain.

### 10.9 Bus Topology Analysis

Analyzes bus wire, bus entry, and bus alias elements:
- Validates bus aliases have corresponding labels
- Checks bus entry placement and connectivity
- Resolves alias member names against actual nets

### 10.10 Wire Geometry

Analyzes physical wire routing in the schematic:
- Total wire count and total length
- Diagonal wire detection (non-orthogonal wires — unusual in schematics)
- Wire segment length statistics

### 10.11 Property Pattern Audit

- Components where <code>value == reference</code> (often indicates unset value)
- Missing or suspicious property values
- Inconsistent property naming

### 10.12 Hierarchical Label Validation

- Hierarchical labels without matching sheet pins
- Sheet pins without matching hierarchical labels
- Orphaned hierarchical connections

---

## 11. Tier 3 Analyses

Higher-level analyses that build on the core extraction and signal detection results.

### 11.1 PDN Impedance Analysis

Estimates power distribution network impedance for each rail:
- Counts bulk and decoupling capacitors per rail
- Estimates impedance at various frequencies using parallel cap model
- Identifies rails with potentially insufficient decoupling

### 11.2 Sleep Current Audit

Estimates quiescent/sleep current by examining:
- IC leakage on each rail (using typical values by IC type)
- Pull-up/pull-down resistor current: <code>I = V / R</code>
- Voltage divider bias current
- LED indicator current
- Reports per-rail and total estimated sleep current

### 11.3 Voltage Derating

Cross-references component voltage ratings (from value or part number) with rail voltages:
- Capacitor voltage rating vs. applied voltage
- Identifies components operating near their voltage limit

### 11.4 Power Budget

Estimates power consumption per rail:
- IC current draw (estimated from typical values)
- LED current
- Resistive loads
- Total power per rail

### 11.5 Power Sequencing

Identifies enable pin chains and power-good signals:
- Maps which regulators' PG outputs connect to which regulators' EN inputs
- Detects sequencing order
- Identifies regulators without sequencing control

### 11.6 BOM Optimization

Identifies consolidation opportunities:
- Multiple resistors/capacitors with similar values that could be unified
- Components with non-standard values that could use preferred values
- Package size consistency within component types

### 11.7 Test Coverage

Evaluates design testability:
- Test point placement relative to key nets
- Accessible probe points
- Nets without test access

### 11.8 Assembly Complexity

Estimates assembly difficulty:
- Component count by package type (SMD vs. through-hole)
- Fine-pitch component identification
- BGA presence
- Mixed-technology boards

### 11.9 USB Compliance

Checks USB-specific design requirements:
- D+/D- series resistors (typically 22Ω–27Ω)
- Pull-up resistor on D+ (full-speed) or D- (low-speed)
- ESD protection on USB lines
- VBUS decoupling

### 11.10 Inrush Current Analysis

Estimates inrush current at power-on:
- Total input capacitance per rail
- Identifies hot-plug scenarios (USB, connector power)
- Checks for inrush limiting (NTC thermistors, soft-start circuits)

---

## 12. BOM Generation

Components are grouped by <code>(value, footprint, lib_id)</code> tuple. Each BOM entry contains:

```python
{
    "value": "10k",
    "footprint": "Resistor_SMD:R_0402_1005Metric",
    "lib_id": "Device:R",
    "references": ["R1", "R2", "R5"],
    "quantity": 3,
    "type": "resistor",
    "mpn": "RC0402FR-0710KL",    # if present in properties
    "dnp": False
}
```

Power symbols (<code>#PWR</code>, <code>#FLG</code>) and DNP components are excluded from the BOM count but DNP components are listed separately.

---

## 13. Known Limitations

1. **Legacy <code>.sch</code> format**: No pin-level net connectivity. Would require parsing the companion <code>.lib</code> file for pin geometry, which is not implemented. Legacy schematics get component lists and label-based net names only.

2. **Value parsing generosity**: <code>parse_value()</code> extracts the first numeric-looking substring. A transistor's <code>"2N3904"</code> could be parsed as <code>2.0</code> if the caller doesn't check component type first. All signal detectors guard against this by checking type before using parsed values.

3. **Coordinate tolerance**: <code>EPSILON = 0.01mm</code>. Points closer than this are treated as the same location. Extremely dense layouts could theoretically have false coordinate merges, though this hasn't been observed in practice across 1,053 tested schematics.

4. **High-fanout filter rejection**: RC/LC filters on nets with >6 connections are rejected as likely buses. This may miss legitimate filters on high-fanout nets (e.g., a filter feeding multiple ICs).

5. **Regulator Vout estimation**: Uses a lookup table + heuristic sweep, not actual SPICE simulation. Accuracy depends on the part being in the lookup table or the feedback divider being properly detected.

6. **Set iteration order**: Some analyses iterate over Python sets, which have non-deterministic ordering. This can cause output field order to vary between runs for certain sections (e.g., <code>protection_devices</code>, <code>pwr_flag_warnings</code>). The data is identical when sorted.

7. **No <code>.lib</code> parsing**: The analyzer reads only <code>.kicad_sch</code> files and their embedded <code>(lib_symbols)</code> sections. External library files (<code>.kicad_sym</code>, <code>.lib</code>) are not parsed. This means all symbol information must be embedded in the schematic — which KiCad does by default when saving.

8. **Solder jumper gates**: Voltage dividers gated by solder jumpers (where opening/closing a jumper changes the divider ratio) are not detected as conditional configurations.

9. **find_deep() false positives**: The <code>find_deep()</code> S-expression search function matches at any depth, which can return nodes from unrelated subtrees. Analysis code uses <code>find_all()</code> (direct children only) where possible and validates context when <code>find_deep()</code> is necessary.

---

## 14. Verification

The analyzer is tested against a corpus of 1,053 open-source KiCad schematics spanning KiCad 4–9, including:
- Simple single-sheet designs (< 10 components)
- Complex multi-sheet hierarchical designs (> 500 components)
- Multi-instance designs (repeated sub-sheets)
- Legacy <code>.sch</code> format files (KiCad 4/5)
- Migrated designs (KiCad 5 → 9 format)

All 1,053 schematics parse and analyze successfully (100% pass rate). A structural validation suite checks:
- Required output keys present
- Component/net counts plausible
- BOM quantities consistent with component counts
- Signal analysis sections present for modern files with components
- No absurd values (>200 entries in any signal analysis category, >500-pin nets, etc.)
