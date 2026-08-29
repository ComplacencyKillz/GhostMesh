---
---
# Manual Schematic Parsing (Script Fallback)

When <code>analyze_schematic.py</code> fails (unsupported format, newer KiCad version, corrupted file), fall back to direct file parsing. This is more expensive (reading raw S-expressions) but always works as long as the file is valid KiCad.

## Table of Contents

1. [When to Use Manual Parsing](#when-to-use-manual-parsing)
2. [File Format Quick Reference](#file-format-quick-reference)
3. [Component Extraction](#component-extraction)
4. [Net Building](#net-building)
5. [Signal Analysis Patterns](#signal-analysis-patterns)
6. [Legacy .sch Format](#legacy-sch-format)
7. [Validation Methodology](#validation-methodology)

---

## When to Use Manual Parsing

Use manual parsing when:
- <code>analyze_schematic.py</code> crashes or returns 0 components on a file you know has content
- The schematic is from a KiCad version newer than the script supports
- You need to validate script output against raw file data
- The file is partially corrupt but still readable

Always try the script first — it handles coordinate transforms, multi-unit symbols, hierarchical sheets, and net building automatically.

---

## File Format Quick Reference

### Modern <code>.kicad_sch</code> (KiCad 6+)

S-expression format. Key sections in order:

<pre><code>
(kicad_sch (version N) (generator ...) (uuid ...)
  (lib_symbols ...)        ; Library symbol definitions (pin data, shapes)
  (junction ...)           ; Wire junction points
  (no_connect ...)         ; Explicit no-connect markers
  (wire ...)               ; Wire segments (coordinate pairs)
  (label ...)              ; Local net labels
  (global_label ...)       ; Cross-sheet net labels
  (hierarchical_label ...) ; Sheet-to-sheet pin labels
  (symbol ...)             ; Placed component instances
  (sheet ...)              ; Sub-sheet references (hierarchical designs)
)
</code></pre>

### Legacy <code>.sch</code> (KiCad 4/5)

Line-based format. Key block types:

<pre><code>
EESchema Schematic File Version N
$Comp / $EndComp          ; Component blocks
Wire Wire Line / x1 y1 x2 y2  ; Wire segments
Text Label / Text GLabel   ; Labels
NoConn ~ x y              ; No-connect markers
$Sheet / $EndSheet         ; Sub-sheet references
</code></pre>

---

## Component Extraction

### Modern Format

Each placed component is a <code>(symbol ...)</code> block after the <code>(lib_symbols)</code> section:

<pre><code>
(symbol (lib_id "Device:R") (at 152.4 176.53 90) (unit 1)
  (property "Reference" "R13" ...)
  (property "Value" "10k" ...)
  (property "Footprint" "Resistor_SMD:R_0402_1005Metric" ...)
  (property "Datasheet" "~" ...)
  (pin "1" (uuid ...))
  (pin "2" (uuid ...))
)
</code></pre>

**Extract for each component:**
- <code>lib_id</code> — library:symbol name
- <code>at</code> — placement position (X, Y, rotation angle)
- <code>unit</code> — which unit of a multi-unit symbol (1-based, e.g., LM324 unit 1-4 + power unit 5)
- Properties: Reference, Value, Footprint, Datasheet, MPN, Manufacturer, etc.

**Filtering:**
- Skip power symbols: <code>lib_id</code> contains <code>:power:</code> or the lib_symbol has a <code>(power)</code> flag
- Skip power flag markers: Reference starts with <code>#PWR</code> or <code>#FLG</code>
- Respect DNP: check for <code>(dnp yes)</code> attribute or <code>"DNP"</code> property

**Multi-unit symbols (critical):**
Symbols like LM324 (quad op-amp), STM32 (multi-bank MCU), dual inductors, relays — each unit is a separate <code>(symbol ...)</code> placement sharing the same Reference. Count unique References for BOM, not placements.

The <code>lib_symbols</code> section contains sub-symbols named <code>SymName_U_V</code> where U = unit number. <code>_0_1</code> sub-symbols contain pins shared by ALL units (typically power pins).

### Legacy Format

Components are in <code>$Comp</code>/<code>$EndComp</code> blocks:

<pre><code>
$Comp
L library:SymbolName Reference
U unit_number convert_num timestamp
P x y
F 0 "R1" ...          ; Reference
F 1 "10k" ...         ; Value
F 2 "footprint" ...   ; Footprint
F 3 "datasheet" ...   ; Datasheet
F 4 "custom" ...      ; Custom field (MPN, Manufacturer, etc.)
    1    x y
$EndComp
</code></pre>

**Custom fields (F4+):** May contain MPN (<code>manf#</code>, <code>MPN</code>, <code>MFG Part</code>), Manufacturer (<code>Manufacturer</code>, <code>MFG</code>), distributor part numbers (<code>DigiKey</code>, <code>Mouser</code>, <code>LCSC</code>), or DNP flag.

---

## Net Building

### Coordinate-Based Union-Find (Modern Format)

KiCad schematics don't store netlists — connectivity is implicit through coordinate matching. Build nets by:

1. **Extract all wire endpoints** from <code>(wire (pts (xy X1 Y1) (xy X2 Y2)))</code> blocks
2. **Compute absolute pin positions** for each component (see <code>net-tracing.md</code> for transforms)
3. **Union-find**: merge coordinate groups connected by wires, junctions, and shared endpoints
4. **Assign net names** from labels (local, global, hierarchical) and power symbols at group endpoints

**Critical rules:**
- **Y-axis inversion**: <code>absolute_Y = symbol_Y - pin_Y</code> (not <code>+</code>)
- **Sheet isolation**: Each sheet has a separate coordinate space. Only global labels and power symbols connect across sheets. Local labels are scoped to their sheet.
- **Junctions**: Wires crossing at a point only connect if there's an explicit <code>(junction (at X Y))</code>. T-junctions (wire endpoint touching mid-wire) also connect.
- **Power symbols connect globally**: All instances of <code>GND</code>, <code>+3V3</code>, etc. are the same net regardless of sheet.

### Net Names

Nets are named by (priority order):
1. Power symbol name (e.g., <code>GND</code>, <code>+3V3</code>, <code>+5V</code>)
2. Global label name
3. Local label name
4. Hierarchical label name
5. Unnamed (auto-generated <code>__unnamed_N</code>)

### Legacy Format

Wires: <code>Wire Wire Line</code> followed by <code>X1 Y1 X2 Y2</code> on next line.
Labels: <code>Text Label X Y orientation 0 ~ 0 "NetName"</code> or <code>Text GLabel ...</code>.
No-connects: <code>NoConn ~ X Y</code>.

---

## Signal Analysis Patterns

When scripts can't detect subcircuits, look for these patterns manually in the component/net data.

### Power Regulators

**LDO pattern:** IC with pins named VIN, VOUT, GND (and optionally EN, PG, ADJ/FB). VIN and VOUT connect to different named power nets.

**Switching regulator pattern:** IC with SW/LX/PH pin connected to an inductor. May also have FB pin with voltage divider, BOOT/BST pin with bootstrap capacitor.

**Pin name variants:**
| Function | Pin names |
|----------|-----------|
| Input | VIN, VI, IN, PVIN, AVIN, INPUT |
| Output | VOUT, VO, OUT, OUTPUT |
| Feedback | FB, VFB, ADJ, VADJ (may have numeric suffix: FB1, ADJ2) |
| Switch | SW, PH, LX (may have numeric suffix: SW1, SW2) |
| Enable | EN, ENABLE, ON, ~{SHDN}, SHDN, ~{EN} |
| Bootstrap | BOOT, BST, BOOTSTRAP, CBST |

**Custom library detection:** If the IC has both VIN and VOUT connected to distinct recognized power nets (e.g., +5V and +3V3), it's almost certainly a regulator even without keyword matches in the library name.

### Voltage Dividers

Two resistors in series: R1_pin1→Net_top, R1_pin2→R2_pin1→Mid_net, R2_pin2→Net_bottom. The mid-point net should NOT be a power rail with many connections (that's pull-ups sharing a bus, not a divider).

<code>ratio = R_bottom / (R_top + R_bottom)</code>

### Op-Amp Circuits

Look for ICs with <code>+IN</code>/<code>IN+</code>, <code>-IN</code>/<code>IN-</code>, <code>OUT</code> pins (or bare <code>+</code>, <code>-</code>, <code>~</code> pin names for KiCad standard library op-amps).

**Multi-unit op-amps (LM324, TL082, etc.):** Each unit has its own +IN/-IN/OUT pins with different pin numbers. When analyzing manually, check the <code>lib_symbols</code> section for <code>SymName_N_1</code> sub-symbols to identify which pins belong to which unit.

**Configurations:**
- **Buffer**: OUT connected directly to -IN
- **Inverting**: Feedback R from OUT to -IN, input R to -IN, +IN to reference/ground
- **Non-inverting**: Feedback R from OUT to -IN, +IN to signal, -IN to ground via R
- **Comparator/open-loop**: No feedback resistor from OUT to -IN

**Common false positives:**
- Current sense amps (INA180/181/185/186/190/199): Have IN+/IN- pins but are fixed-gain, not user-configurable op-amps
- Digital power monitors (INA219/226/229): Have I2C interface, not analog op-amp pins
- Analog front-ends (AD8233): Complex ICs with internal op-amps that don't follow standard topology

### Transistor Circuits

**N-channel MOSFET:** Look for Q references with <code>NMOS</code>/<code>N-Channel</code> in lib_id or ki_keywords. Gate→drive signal, Drain→load, Source→GND (low-side switch).

**P-channel MOSFET:** <code>PMOS</code>/<code>P-Channel</code> in lib_id or ki_keywords. Source→power rail, Drain→load, Gate→control (inverted logic). Used as high-side switches.

**Reliable P-channel detection (priority order):**
1. <code>ki_keywords</code> from lib_symbol containing "P-Channel" — most reliable
2. lib_id containing <code>pmos</code>, <code>p-channel</code>, <code>q_pmos</code>
3. Value containing unambiguous P-channel family names (DMP series from Diodes Inc)

**Bridge circuits:** Look for transistor pairs where one drain connects to another's source (half-bridge mid-point). Two such pairs = H-bridge. Three = 3-phase.

### Protection Devices

TVS/ESD diodes: Keywords <code>TVS</code>, <code>ESD</code>, <code>PESD</code>, <code>PRTR</code>, <code>USBLC</code>, <code>SMAJ</code>, <code>SMBJ</code>, <code>LESD</code> in value or lib_id. Connected between signal line and ground/power.

ESD protection ICs: <code>USBLC6</code>, <code>PRTR5V</code>, <code>SP0502</code>, <code>TPD4E05</code> etc. Multi-channel protection arrays.

### Bus Detection

**I2C:** Nets named <code>SDA</code>/<code>SCL</code> (or containing these substrings), or IC pins named <code>SDA</code>/<code>SCL</code>. Look for pull-up resistors (2.2k-10k) to VCC. Exclude <code>SCLK</code>/<code>SCK</code> pins (SPI, not I2C).

**SPI:** Nets named <code>MOSI</code>/<code>MISO</code>/<code>SCK</code>/<code>CS</code> or <code>COPI</code>/<code>CIPO</code>/<code>SCK</code>/<code>CS</code>.

**UART:** Nets named <code>TX</code>/<code>RX</code>/<code>TXD</code>/<code>RXD</code> (exclude nets also containing <code>CAN</code>, <code>SPI</code>, <code>I2C</code>).

**CAN:** Nets named <code>CANH</code>/<code>CANL</code> or CAN transceiver ICs (MCP2551, SN65HVD230, TJA1050, etc.). Don't confuse with RS-485 (SN65HVD75 is RS-485, not CAN).

---

## Legacy .sch Format

### What the Analyzer Provides

The analyzer now parses <code>.lib</code> files (cache libraries and project libs) to populate pin data for legacy schematics. When <code>.lib</code> files are available:

- All component references, values, footprints, lib_ids
- Pin positions, pin names, and pin types (from <code>.lib</code> files)
- Pin-to-net mapping via wire connectivity + pin positions
- Signal analysis (voltage dividers, regulators, op-amp circuits, etc.)
- Subcircuit detection (IC + 1-hop neighbors)
- Net names from labels, power symbols, and pin associations
- Custom properties (F4+ fields: MPN, manufacturer, distributor PNs)

### Remaining Limitations

- **Pin coverage depends on <code>.lib</code> availability** — components whose <code>.lib</code> files aren't in the repo (standard KiCad system libs like <code>power</code>, <code>device</code>, <code>conn</code>) use built-in fallbacks for common symbols (R, C, L, D, LED, transistors). Uncommon standard library symbols may lack pin data.
- **No ki_keywords** — P-channel detection relies on lib_id and value only

### Hierarchical Legacy Designs

Top-level <code>.sch</code> has <code>$Sheet</code> blocks with <code>F1 "subsheet.sch"</code> pointing to sub-sheet files. Parse all sub-sheets. Hierarchical labels in sub-sheets connect to pins on the sheet block in the parent.

---

## Validation Methodology

When verifying analyzer output (or your own manual parse) against the raw schematic:

### Component Count Validation

1. Count all <code>(symbol (lib_id ...))</code> blocks after <code>(lib_symbols)</code> section
2. Subtract power symbols (<code>#PWR</code>, <code>#FLG</code> references)
3. Result should exactly match the analyzer's <code>component_count</code>

### Net Count Validation

1. Count all <code>(wire ...)</code> blocks to verify wire count
2. The number of unique named nets should match approximately (unnamed nets may differ in grouping)
3. Spot-check 3-5 specific nets by tracing pins → wires → labels manually

### Signal Analysis Validation

For each detected subcircuit:
1. Verify the component IS what the analyzer says (check lib_id, value)
2. Verify the pin connections are as reported (trace through nets)
3. Check for false positives: is this detection actually correct?
4. Check for false negatives: are there obvious subcircuits the analyzer missed?

**Severity guide:**
- **HIGH**: Wrong component data (extraction bug) or grossly incorrect detection
- **MEDIUM**: Misleading detection (regulator classified wrong topology, wrong gain)
- **LOW**: Minor cosmetic issue (missing unit number, suboptimal configuration label)

### Known Edge Cases

- **Custom libraries**: Components from project-specific libraries may lack keywords that standard KiCad libraries have. Regulators, op-amps, and transistors from custom libs may not be detected.
- **Multi-unit symbols**: LM324 (quad op-amp), dual inductors, relays — each unit needs separate analysis. Pin numbers are unit-specific.
- **Unit-0 shared pins**: In KiCad lib_symbols, <code>_0_1</code> sub-symbols contain pins shared by all units (typically power: VCC, GND). These must be included with every placed unit.
- **Rescue libraries**: KiCad creates <code>*-rescue</code> libraries during migration. Check for <code>lib_prefix == "power"</code> exactly, not substring match (e.g., <code>dc-power-supply-rescue</code> is NOT a power library).
- **Eagle .sch files**: Not KiCad format — will output 0 components. These are XML or binary and require separate tools.
