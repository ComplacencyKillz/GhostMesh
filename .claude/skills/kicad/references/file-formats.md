---
---
# KiCad File Format Reference

Detailed field-by-field documentation for all KiCad file types. Consult this when manually parsing or inspecting raw KiCad files.

## Table of Contents

1. [S-Expression Format Basics](#s-expression-format-basics)
2. [Schematic (.kicad_sch)](#schematic-kicad_sch)
3. [PCB Layout (.kicad_pcb)](#pcb-layout-kicad_pcb)
4. [Symbol Library (.kicad_sym)](#symbol-library-kicad_sym)
5. [Footprint (.kicad_mod)](#footprint-kicad_mod)
6. [Custom Design Rules (.kicad_dru)](#custom-design-rules-kicad_dru)
7. [Netlist (.net)](#netlist-net)
8. [Legacy KiCad 5 Schematic (.sch)](#legacy-kicad-5-schematic-sch)
9. [Project File (.kicad_pro)](#project-file-kicad_pro)

---

## S-Expression Format Basics

All modern KiCad files use Lisp-like S-expressions: <code>(keyword value1 (child_keyword value2) ...)</code>.

- **Coordinates**: millimeters, origin top-left, X right, Y **down**
- **Angles**: degrees, counterclockwise positive
- **UUIDs**: stable object identifiers (KiCad 6+)
- **Strings**: quoted if containing spaces/special chars

---

## Schematic (<code>.kicad_sch</code>)

### Top-Level Structure
<pre><code>
(kicad_sch
  (version ...) (generator ...) (generator_version ...) (uuid ...) (paper ...)
  (lib_symbols ...)        ; Embedded copies of all library symbols used
  (junction ...)           ; Wire junction points
  (no_connect ...)         ; No-connect markers
  (wire ...)               ; Electrical wires: (wire (pts (xy X1 Y1) (xy X2 Y2)) (uuid ...))
  (bus ...)                ; Bus wires
  (bus_entry ...)          ; Bus entry connections
  (label ...)              ; Local net labels: (label "NAME" (at X Y ANGLE) ...)
  (global_label ...)       ; Global labels (cross-sheet nets)
  (hierarchical_label ...) ; Sheet-to-sheet connections
  (text ...)               ; Annotation text (not electrical)
  (polyline ...)           ; Graphical lines
  (symbol ...)             ; Placed component instances
  (sheet ...)              ; Hierarchical sub-sheet references
  (sheet_instances ...)    ; Sheet path/page info
)
</code></pre>

### Symbol Instance (placed component)
<pre><code>
(symbol
  (lib_id "Library:SymbolName")     ; Library reference
  (at X Y ANGLE)                     ; Position
  (unit N)                           ; For multi-unit symbols
  (uuid "...")
  (property "Reference" "R1" (at ...) (effects ...))
  (property "Value" "10K" (at ...) (effects ...))
  (property "Footprint" "Resistor_SMD:R_0805_2012Metric" ...)
  (property "Datasheet" "~" ...)
  (property "Description" "Resistor" ...)
  ; Custom properties (user-defined):
  (property "Mfg Part" "RC0805FR-0710KL" ...)
  (property "DigiKey Part" "311-10.0KCRCT-ND" ...)
  (pin "1" (uuid "..."))            ; Pin-to-UUID mapping
  (pin "2" (uuid "..."))
  (instances
    (project "ProjectName"
      (path "/root-uuid" (reference "R1") (unit 1))
    )
  )
)
</code></pre>

### Extracting Key Info from Schematics

**Component list (BOM)**: Collect all <code>(symbol ...)</code> nodes. For each, read:
- <code>(property "Reference" ...)</code> - designator (R1, C5, U3)
- <code>(property "Value" ...)</code> - value (10K, 100n, STM32F407)
- <code>(property "Footprint" ...)</code> - PCB footprint
- <code>(property "Mfg Part" ...)</code> / <code>(property "DigiKey Part" ...)</code> - sourcing
- <code>(in_bom yes/no)</code> - whether to include in BOM
- <code>(dnp yes/no)</code> - Do Not Populate flag

**Net connectivity**: Nets are implicit in schematics, formed by:
1. **Wires** connecting pin endpoints
2. **Junctions** where wires cross and connect
3. **Labels** (<code>label</code> = local to sheet, <code>global_label</code> = all sheets)
4. **Power symbols** (e.g., <code>+3V3</code>, <code>GND</code>) create implicit global nets
5. **Hierarchical labels** + sheet pins connect parent/child sheets

For detailed step-by-step net tracing with coordinate math and rotation transforms, read <code>net-tracing.md</code>.

**Power rails**: Look for symbols with <code>lib_id</code> starting with <code>power:</code> (e.g., <code>power:+3V3</code>, <code>power:GND</code>). These create global nets named after their value.

### Hierarchical Sheets
<pre><code>
(sheet (at X Y) (size W H) (uuid "...")
  (property "Sheetname" "PowerSupply" ...)
  (property "Sheetfile" "power_supply.kicad_sch" ...)
  (pin "VIN" input (at X Y ANGLE) (uuid "..."))
)
</code></pre>
Each sheet has its own <code>.kicad_sch</code> file. Pins on the sheet symbol connect to <code>hierarchical_label</code> nodes inside.

---

## PCB Layout (<code>.kicad_pcb</code>)

### Top-Level Structure
<pre><code>
(kicad_pcb
  (version ...) (generator ...) (generator_version ...)
  (general (thickness 1.6) (legacy_teardrops no))
  (paper "A4")
  (layers ...)              ; Layer stack definition
  (setup ...)               ; Board setup, stackup, plot params
  (net 0 "")                ; KiCad ≤9: net declarations (index + name)
  (net 1 "GND")             ; KiCad 10: no net declarations — nets identified by name
  (net 2 "+3V3")
  ...
  (footprint ...)           ; Placed footprints with pads
  (segment ...)             ; Copper track segments
  (arc ...)                 ; Curved tracks (KiCad 7+)
  (via ...)                 ; Vias
  (zone ...)                ; Copper zones/pours
  (gr_line ...)             ; Graphical lines (board outline on Edge.Cuts)
  (gr_arc ...)              ; Graphical arcs
  (gr_circle ...)           ; Graphical circles
  (gr_rect ...)             ; Graphical rectangles
  (gr_text ...)             ; Board text
  (group ...)               ; Groups (KiCad 6+)
)
</code></pre>

### Layer Definitions
<pre><code>
(layers
  (0 "F.Cu" signal)         ; Front copper
  (2 "B.Cu" signal)         ; Back copper (number varies by version)
  (1 "In1.Cu" signal)       ; Inner copper layers (if present)
  (5 "F.SilkS" user)        ; Front silkscreen
  (7 "B.SilkS" user)        ; Back silkscreen
  (25 "Edge.Cuts" user)     ; Board outline
  (31 "F.CrtYd" user)       ; Front courtyard
  (35 "F.Fab" user)         ; Front fabrication
  ...
)
</code></pre>
**Note**: Layer numbers differ between versions. In KiCad 5: F.Cu=0, B.Cu=31, inner=1-30. In KiCad 6+: B.Cu number depends on layer count (e.g., B.Cu=2 for 2-layer, B.Cu=4 for 4-layer).

### Footprint on PCB
<pre><code>
(footprint "Package:SOT-563"
  (layer "F.Cu")                    ; Which side of board
  (uuid "...")
  (at X Y ANGLE)                    ; Position and rotation
  (property "Reference" "U2" ...)
  (property "Value" "TPS61023" ...)
  (property "Mfg Part" "TPS61023DRLR" ...)
  (property "DigiKey Part" "296-TPS61023DRLRCT-ND" ...)
  (path "/schematic-uuid")          ; Links back to schematic symbol
  (sheetname "/")
  (sheetfile "project.kicad_sch")
  (attr smd)                        ; smd | through_hole
  ; Graphics (silkscreen, courtyard, fab):
  (fp_line (start X1 Y1) (end X2 Y2) (layer "F.SilkS") ...)
  (fp_rect (start X1 Y1) (end X2 Y2) (layer "F.CrtYd") ...)
  ; Pads with net assignments:
  (pad "1" smd roundrect
    (at X Y ANGLE)
    (size W H)
    (layers "F.Cu" "F.Mask" "F.Paste")
    (net 5 "+3V3")                  ; KiCad ≤9: (net number "name")
                                     ; KiCad 10: (net "name") — no numeric ID
    (pintype "passive")
    (uuid "...")
  )
  ; 3D model:
  (model "path/to/model.wrl" (offset ...) (scale ...) (rotate ...))
)
</code></pre>

### Tracks, Vias, and Zones
<pre><code>
; KiCad ≤9: net referenced by integer ID
(segment (start X1 Y1) (end X2 Y2) (width 0.2) (layer "F.Cu") (net 7) (uuid "..."))
(via (at X Y) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net 7) (uuid "..."))
(zone (net 1) (net_name "GND") (layer "F.Cu") (uuid "...") ...)

; KiCad 10: net referenced by name string, no net_name node on zones
(segment (start X1 Y1) (end X2 Y2) (width 0.2) (layer "F.Cu") (net "NetName") (uuid "..."))
(via (at X Y) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net "NetName") (uuid "..."))
(zone (net "GND") (layer "F.Cu") (uuid "...") ...)

; Zone structure (both versions):
(zone (net ...) ... (layer "F.Cu") (uuid "...")
  (connect_pads (clearance 0.25))
  (min_thickness 0.25)
  (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))
  (polygon (pts (xy X1 Y1) (xy X2 Y2) ...))      ; User-drawn outline
  (filled_polygon (pts ...))                        ; Computed fill result
)
</code></pre>

### Board Outline
Look for graphical items on <code>Edge.Cuts</code> layer:
<pre><code>
(gr_line (start X1 Y1) (end X2 Y2) (layer "Edge.Cuts") ...)
(gr_arc (start ...) (mid ...) (end ...) (layer "Edge.Cuts") ...)
</code></pre>

### Tracing Net Connectivity on PCB
To find everything connected to a net:

**KiCad ≤9** (integer net IDs):
1. Find <code>(net N "NetName")</code> in the net declarations
2. Find all <code>(pad ... (net N "name") ...)</code> in footprints
3. Find all <code>(segment ... (net N) ...)</code> — copper traces
4. Find all <code>(via ... (net N) ...)</code> — layer transitions
5. Find all <code>(zone (net N) ...)</code> — copper pours

**KiCad 10** (string net names — no net declarations section):
1. Find all <code>(pad ... (net "NetName") ...)</code> in footprints
2. Find all <code>(segment ... (net "NetName") ...)</code> — copper traces
3. Find all <code>(via ... (net "NetName") ...)</code> — layer transitions
4. Find all <code>(zone (net "NetName") ...)</code> — copper pours

---

## Symbol Library (<code>.kicad_sym</code>)

<pre><code>
(kicad_symbol_lib
  (version ...) (generator ...) (generator_version ...)
  (symbol "SymbolName"
    (pin_names (offset 0) (hide yes))
    (exclude_from_sim no) (in_bom yes) (on_board yes)
    (property "Reference" "R" ...)
    (property "Value" "R" ...)
    (property "Footprint" "" ...)
    (property "Datasheet" "~" ...)
    (property "Description" "Resistor" ...)
    (property "ki_keywords" "R res resistor" ...)
    (property "ki_fp_filters" "R_*" ...)      ; Footprint filter patterns

    (symbol "SymbolName_0_1"                   ; Unit 0 = shared graphics
      (rectangle (start ...) (end ...) ...)
    )
    (symbol "SymbolName_1_1"                   ; Unit 1 pins
      (pin TYPE SHAPE (at X Y ANGLE) (length L)
        (name "PinName" ...)
        (number "1" ...)
      )
    )
  )
)
</code></pre>

**Pin types**: <code>input</code>, <code>output</code>, <code>bidirectional</code>, <code>tri_state</code>, <code>passive</code>, <code>free</code>, <code>unspecified</code>, <code>power_in</code>, <code>power_out</code>, <code>open_collector</code>, <code>open_emitter</code>, <code>no_connect</code>

**Multi-unit naming**: <code>SymbolName_UNIT_STYLE</code> - unit 0 = common, units 1+ = per-unit

---

## Footprint (<code>.kicad_mod</code>)

Stored in <code>.pretty/</code> directories (one file per footprint).

<pre><code>
(footprint "FootprintName"
  (version ...) (generator ...) (layer "F.Cu")
  (descr "Description text")
  (tags "tag1 tag2")
  (attr smd|through_hole|board_only)
  (property "Reference" "REF**" (layer "F.SilkS") ...)
  (property "Value" "FootprintName" (layer "F.Fab") ...)
  ; Pads (no net assignment in library, only on PCB):
  (pad "1" smd roundrect (at X Y) (size W H) (layers "F.Cu" "F.Mask" "F.Paste") ...)
  (pad "2" thru_hole circle (at X Y) (size W H) (drill D) (layers "*.Cu" "*.Mask") ...)
  ; 3D model:
  (model "path.wrl" ...)
)
</code></pre>

**Pad types**: <code>smd</code>, <code>thru_hole</code>, <code>np_thru_hole</code> (non-plated), <code>connect</code> (edge connector)
**Pad shapes**: <code>circle</code>, <code>rect</code>, <code>oval</code>, <code>roundrect</code>, <code>trapezoid</code>, <code>custom</code>

---

## Custom Design Rules (<code>.kicad_dru</code>)

Text-based constraint rules applied during DRC. Example:
<pre><code>
(version 1)
(rule "Track width, outer layer"
  (layer outer)
  (condition "A.Type == 'track'")
  (constraint track_width (min 0.127mm))
)
(rule "Clearance: pad to pad"
  (condition "A.isPlated() && B.isPlated() && A.Net != B.Net")
  (constraint clearance (min 0.127mm))
)
</code></pre>
Useful for enforcing manufacturer capabilities (e.g., JLCPCB, PCBWay).

---

## Netlist (<code>.net</code>)

<pre><code>
(export (version D)
  (components
    (comp (ref U1)
      (value STM32F407ZGTx)
      (footprint Package_QFP:LQFP-144_20x20mm_P0.5mm)
      (libsource (lib MCU_ST_STM32F4) (part STM32F407ZGTx) (description "..."))
      (sheetpath (names /) (tstamps /))
      (tstamp HEXID)
    )
  )
  (nets
    (net (code 1) (name GND)
      (node (ref U1) (pin 12))
      (node (ref C1) (pin 2))
    )
  )
)
</code></pre>
The netlist explicitly lists every net and which component pins belong to it.

---

## Legacy KiCad 5 Schematic (<code>.sch</code>)

<pre><code>
EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Comp                                    ; Component instance
L Library:Symbol Reference
U unit timestamp
P x y
F 0 "R1" H x y size flags C CNN        ; F0 = Reference
F 1 "10k" H x y size flags C CNN        ; F1 = Value
F 2 "Footprint:Name" H x y size flags   ; F2 = Footprint
F 3 "datasheet_url" H x y size flags    ; F3 = Datasheet
  1    x y                               ; Instance position
  orientation_matrix
$EndComp
Wire Wire Line                           ; Electrical wire
  x1 y1 x2 y2
Text Label x y orientation size ~ 0      ; Local net label
NetName
</code></pre>

**Key differences from modern format:**
- Coordinates in mils (1/1000 inch), not mm
- No UUIDs, uses timestamps for identification
- Component fields are positional (F0=ref, F1=value, F2=footprint, F3=datasheet)
- Wire connectivity is purely positional (endpoints must coincide exactly)

---

## Project File (<code>.kicad_pro</code>)

JSON format. Key sections:
- <code>board.design_settings</code> - DRC rules, track widths, via sizes, teardrop settings
- <code>board.design_settings.rules</code> - min clearance, hole sizes, track widths
- <code>board.design_settings.track_widths</code> - available track width options
- <code>board.design_settings.via_dimensions</code> - available via size options
- <code>erc.rule_severities</code> - ERC check severity levels
- <code>net_settings.classes</code> - net class definitions (clearance, track width, via size per class)
- <code>schematic.bom_settings</code> - BOM field configuration
- <code>schematic.drawing</code> - default text/line sizes
