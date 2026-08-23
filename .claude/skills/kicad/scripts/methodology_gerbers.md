---
---
# Gerber & Drill File Analyzer — Methodology

This document describes the analysis methodology used by <code>analyze_gerbers.py</code>. It covers Gerber RS-274X parsing, Excellon drill parsing, layer identification, X2 attribute extraction, and all higher-level analyses performed on a fabrication output directory.

## Design Philosophy

Same as the schematic and PCB analyzers — this is a **data extraction layer**. It outputs structured JSON containing neutral observations about the fabrication files. An LLM (or human reviewer) consumes this alongside the schematic and PCB layout analyses for design review.

The Gerber analyzer is the last line of defense before a board order — the fabrication files are what the manufacturer actually builds. Thorough detection matters: a missing layer, stale zip archive, or misaligned coordinate range that goes unreported can result in costly respins. Every reported fact (layer identification, drill classification, coordinate extents) must be accurate, since the reviewer is making go/no-go ordering decisions based on this data.

The analyzer operates on manufactured output files, not source design files. It answers the question "what did the CAD tool actually produce?" rather than "what did the designer intend?" This makes it useful for:
- Verifying that exported Gerbers match the design (cross-reference with PCB analysis)
- Checking fabrication file completeness before ordering
- Extracting board specs and design rules from machine-readable metadata
- Identifying layer alignment issues that would cause manufacturing defects

---

## 1. Input Discovery

### File Collection

<code>analyze_gerbers()</code> scans the target directory for three file categories:

| Category | Extensions | Purpose |
|---|---|---|
| Gerber files | <code>.gbr</code>, <code>.g*</code> (including Protel: <code>.gtl</code>, <code>.gbl</code>, <code>.gts</code>, <code>.gbs</code>, <code>.gto</code>, <code>.gbo</code>, <code>.gko</code>, <code>.gm1</code>, <code>.g1</code>–<code>.g4</code>) | Copper, mask, paste, silk, edge layers |
| Drill files | <code>.drl</code> | Excellon hole data (PTH and NPTH) |
| Job files | <code>.gbrjob</code> | KiCad-generated JSON metadata |
| Zip archives | <code>.zip</code> | Packaged Gerber sets (scanned for staleness, not extracted) |

Both lowercase and uppercase extensions are collected. Duplicates are removed, and non-Gerber files (<code>.drl</code>, <code>.gbrjob</code>, <code>.zip</code>, <code>.pos</code>) are filtered from the Gerber list.

### Processing Order

1. Parse all Gerber files → <code>gerbers[]</code>
2. Parse all drill files → <code>drills[]</code>
3. Parse job file (first <code>.gbrjob</code> found, if any) → <code>job_info</code>
4. Run analysis functions over the parsed data

Individual file parse errors are caught and recorded as <code>{"error": "..."}</code> entries — a corrupt file doesn't abort the entire analysis.

---

## 2. Gerber RS-274X Parsing

<code>parse_gerber()</code> performs a single stateful pass over each Gerber file, extracting format information, aperture definitions, X2 attributes, operation counts, and coordinate ranges.

### 2.1 Format and Units

Extracted via regex from the file content:

- **Format specification** (<code>%FS...%</code>): Zero omission mode (leading/trailing), coordinate notation (absolute/incremental), and integer/decimal digit counts for X and Y axes. These are needed to interpret raw coordinate integers.
- **Units** (<code>%MOIN*%</code> or <code>%MOMM*%</code>): Inch or millimeter. Determines whether aperture dimensions need conversion.

### 2.2 Two-Phase Architecture

**Phase 1** (regex over full content): Extracts format, units, and X2 file attributes (<code>%TF.*%</code> and <code>G04 #@! TF.*</code> comment format). These are needed before the line-by-line pass can interpret coordinates.

**Phase 2** (stateful line-by-line): Tracks aperture state, object attributes, and operations:

| State Machine | Tracked By | Purpose |
|---|---|---|
| Aperture attributes | <code>pending_aper_function</code> | TA.AperFunction preceding AD definition |
| Current component | <code>current_component</code> | TO.C object attribute |
| Current net | <code>current_net</code> | TO.N object attribute |
| Component pad counts | <code>component_pads{}</code> | Flash count per component ref |
| Component net sets | <code>component_nets{}</code> | Which nets each component connects to |
| Pin mappings | <code>pin_mappings[]</code> | TO.P ref/pin/pin_name/net tuples |

### 2.3 X2 File Attributes

Two formats are supported:

- **Modern** (KiCad 6+): <code>%TF.Key,Value*%</code> — standard X2 extended attributes
- **KiCad 5 comment format**: <code>G04 #@! TF.Key,Value*</code> — same data embedded in G-code comments

The comment format is checked second, so modern attributes take precedence if both exist.

Common file attributes extracted:
- <code>FileFunction</code> — layer type (Copper, SolderMask, Legend, Profile, etc.)
- <code>FilePolarity</code> — Positive or Negative
- <code>GenerationSoftware</code> — CAD tool identification
- <code>CreationDate</code> — when the file was generated

### 2.4 X2 Object Attributes

Object attributes (<code>TO.*</code>) track which component, net, and pin are associated with subsequent operations:

- **<code>TO.C,RefDes</code>** — sets the current component reference (e.g., <code>TO.C,R1</code>)
- **<code>TO.N,NetName</code>** — sets the current net name
- **<code>TO.P,Ref,Pin[,PinName]</code>** — records a pin-to-net mapping

These are accumulated across all flashes and draws. When a flash (D03) occurs while <code>current_component</code> is set, the pad count for that component increments. This builds a component-level view of which pads exist on each layer.

### 2.5 Aperture Definitions

Each <code>%AD...%</code> definition is parsed for:
- **D-code**: The aperture identifier (D10, D11, ...)
- **Type**: C (circle), R (rectangle), O (obround), RoundRect (macro), or custom
- **Parameters**: Dimensions in file units
- **Function**: If a <code>TA.AperFunction</code> preceded the definition, it's attached (e.g., <code>Conductor</code>, <code>SMDPad,CuDef</code>, <code>ViaPad</code>, <code>HeatsinkPad</code>)

### 2.6 Aperture Dimension Parsing

<code>_parse_aperture_dimension()</code> extracts the primary dimension (in mm) from standard aperture types:

| Aperture Type | Dimension Extracted |
|---|---|
| C (circle) | Diameter |
| R (rectangle) | Smaller of width/height |
| O (obround) | Smaller of width/height |
| RoundRect | 2× corner radius (conservative estimate) |

Inch dimensions are converted to mm. These are used for trace width distribution and minimum feature size analysis.

### 2.7 Operation Counting

Three operation types are counted:
- **Flashes** (D03): Pad/via placements — counted globally and per component
- **Draws** (D01): Trace segments, arcs
- **Regions** (G36): Copper fills/pours

### 2.8 Coordinate Range

Every <code>X...Y...</code> coordinate in the file updates a running min/max bounding box. This is used later for layer alignment checking and board dimension estimation. Coordinates are divided by the format's decimal precision to get real-world values.

### 2.9 Aperture Analysis Summary

After the line-by-line pass, aperture data is aggregated:
- **By function**: Count of apertures per function category (SMDPad, ViaPad, Conductor, HeatsinkPad, etc.)
- **Conductor widths**: Set of unique trace widths (mm) from Conductor-tagged apertures
- **Minimum feature**: Smallest aperture dimension across all types

---

## 3. Excellon Drill Parsing

<code>parse_drill()</code> parses Excellon drill files in a single pass.

### 3.1 Header Parsing

The header (before the <code>%</code> end-of-header marker) contains:
- **Units**: Detected from <code>METRIC</code>/<code>MOMM</code> or <code>INCH</code> keywords
- **Tool definitions**: <code>TnnCddd.ddd</code> — tool number and diameter. Diameters in inches are converted to mm.
- **X2 attributes**: Same <code>; #@! TF.*</code> comment format as Gerber files
- **Per-tool aperture functions**: <code>; #@! TA.AperFunction,Plated,PTH,ViaDrill</code> etc. — attached to the next tool definition

### 3.2 Drill Hits

After the header, tool selections (<code>Tnn</code>) and coordinate lines (<code>Xnnn.nnnYnnn.nnn</code>) are tracked. Each coordinate line increments the hole count for the current tool and updates the coordinate bounding box.

### 3.3 PTH/NPTH Classification

Each drill file is classified as PTH (plated through-hole) or NPTH (non-plated) using:
1. **X2 FileFunction attribute** — <code>Plated</code> or <code>NonPlated</code> (authoritative)
2. **Filename pattern** — <code>pth</code>/<code>npth</code> in the filename (fallback)
3. **Unknown** — if neither source provides classification

### 3.4 Layer Span

From <code>FileFunction</code> values like <code>Plated,1,4,PTH</code>, the layer span is extracted (e.g., layers 1–4). This indicates which copper layers the drill connects and is used to determine total layer count.

---

## 4. Layer Identification

<code>identify_layer_type()</code> maps each Gerber file to a KiCad-style layer name. Three identification methods are tried in priority order:

### 4.1 X2 FileFunction (Highest Priority)

The <code>FileFunction</code> attribute provides authoritative layer identification:

| FileFunction Contains | Mapped Layer |
|---|---|
| <code>copper</code> + <code>top</code> | F.Cu |
| <code>copper</code> + <code>bot</code> | B.Cu |
| <code>copper,Ln,inr</code> | In(n-1).Cu (L2→In1, L3→In2, etc.) |
| <code>soldermask</code> + <code>top</code>/<code>bot</code> | F.Mask / B.Mask |
| <code>paste</code>/<code>solderpaste</code> + <code>top</code>/<code>bot</code> | F.Paste / B.Paste |
| <code>legend</code>/<code>silkscreen</code> + <code>top</code>/<code>bot</code> | F.SilkS / B.SilkS |
| <code>profile</code> | Edge.Cuts |

Inner copper layer mapping: X2 uses absolute layer positions (L2 = second copper layer), while KiCad names inner layers starting at In1.Cu. The conversion is <code>In(L-1).Cu</code>.

### 4.2 KiCad Filename Patterns (Second Priority)

If no X2 attributes are present, the filename is checked against KiCad-style patterns:
- Inner copper: <code>In1_Cu</code>, <code>In1.Cu</code>, etc.
- Outer layers: <code>F_Cu</code>, <code>F.Cu</code>, <code>Front_Cu</code>, <code>B_Cu</code>, etc.
- Masks/paste/silk/edge: Similar patterns with layer prefixes

### 4.3 Protel-Style Extensions (Lowest Priority)

Classic Protel/Altium extensions as a final fallback:

| Extension | Layer |
|---|---|
| <code>.gtl</code> / <code>.gbl</code> | F.Cu / B.Cu |
| <code>.gts</code> / <code>.gbs</code> | F.Mask / B.Mask |
| <code>.gtp</code> / <code>.gbp</code> | F.Paste / B.Paste |
| <code>.gto</code> / <code>.gbo</code> | F.SilkS / B.SilkS |
| <code>.gm1</code> / <code>.gko</code> | Edge.Cuts |
| <code>.g1</code>–<code>.g4</code> | In1.Cu–In4.Cu |

Files that match none of these patterns get <code>"unknown"</code> and are still included in the output.

---

## 5. Job File Parsing

<code>parse_job_file()</code> parses the <code>.gbrjob</code> file (JSON format, generated by KiCad alongside Gerbers).

### Extracted Fields

| JSON Path | Output Field | Purpose |
|---|---|---|
| <code>Header.GenerationSoftware</code> | <code>generator</code>, <code>vendor</code> | CAD tool identification |
| <code>Header.CreationDate</code> | <code>creation_date</code> | Timestamp |
| <code>GeneralSpecs.Size</code> | <code>board_width_mm</code>, <code>board_height_mm</code> | Authoritative board dimensions |
| <code>GeneralSpecs.LayerNumber</code> | <code>layer_count</code> | Total copper layers |
| <code>GeneralSpecs.BoardThickness</code> | <code>board_thickness_mm</code> | Stackup thickness |
| <code>GeneralSpecs.Finish</code> | <code>finish</code> | Surface finish (HASL, ENIG, etc.) |
| <code>GeneralSpecs.ProjectId</code> | <code>project_name</code> | KiCad project name |
| <code>DesignRules[]</code> | <code>design_rules[]</code> | Pad-to-pad, track-to-track, min width, etc. |
| <code>FilesAttributes[]</code> | <code>expected_files[]</code> | What files should exist (for completeness check) |
| <code>MaterialStackup[]</code> | <code>stackup[]</code> | Layer types, thicknesses, materials |

---

## 6. Analysis Functions

### 6.1 Drill Classification

<code>classify_drill_tools()</code> categorizes every drill tool across all drill files into three groups:

**Classification priority:**
1. **NPTH file** → all tools classified as mounting holes (regardless of diameter)
2. **X2 AperFunction** — <code>ViaDrill</code> → via, <code>ComponentDrill</code> → component hole
3. **Diameter heuristic** (fallback when no X2 data):

| Diameter Range | Classification | Rationale |
|---|---|---|
| ≤ 0.45 mm | Via | Standard via drill sizes |
| 0.45–1.3 mm | Component hole | THT component pin sizes |
| > 1.3 mm | Mounting hole | Screws, standoffs |

The output records which method was used (<code>x2_attributes</code> or <code>diameter_heuristic</code>), so the consumer knows confidence level.

### 6.2 Layer Completeness

<code>check_completeness()</code> verifies that all necessary layers are present.

**With <code>.gbrjob</code>:** Compares found layers against the <code>expected_files</code> list from the job file. Reports missing and extra layers. Source is tagged as <code>"gbrjob"</code>.

**Without <code>.gbrjob</code>:** Checks against a default required set:
- **Required**: F.Cu, B.Cu, F.Mask, B.Mask, Edge.Cuts, plus any inner copper layers found
- **Recommended**: F.SilkS, F.Paste
- **Drill**: At least one PTH drill file

A board is <code>"complete": true</code> only when all required layers are present and a PTH drill exists.

### 6.3 Layer Alignment

<code>check_alignment()</code> checks that copper and edge layers have consistent coordinate ranges.

**Method:**
1. Compute width and height from the coordinate bounding box of each identified layer
2. Compare copper layers (F.Cu, B.Cu, inner copper) and Edge.Cuts
3. Flag as misaligned if width or height varies by more than **2.0 mm** across these layers

The 2mm threshold is generous — any real misalignment would produce offsets much larger than normal coordinate range variation. Drill file extents are recorded but not included in the alignment check (drill coordinate ranges are often slightly different due to pad-center vs edge-of-trace differences).

### 6.4 Board Dimensions

<code>compute_board_dimensions()</code> determines board width, height, and area.

**Priority:**
1. **<code>.gbrjob</code>** — <code>GeneralSpecs.Size.X</code> and <code>.Y</code> (authoritative, computed by KiCad)
2. **Edge.Cuts extents** — bounding box of the board outline Gerber file (fallback)

The source is tagged in the output so the consumer knows which method was used. The Edge.Cuts fallback gives bounding-box dimensions, which are correct for rectangular boards but overestimate for boards with cutouts or non-rectangular outlines.

### 6.5 Component Analysis

<code>build_component_analysis()</code> merges X2 object attribute data across all Gerber layers to build a board-level view of components.

**Only produces output when X2 TO attributes are present** (KiCad 6+ exports). Without X2 data, no component analysis is possible from Gerbers alone.

**Merging logic:**
- Component references are collected from all layers that have <code>TO.C</code> attributes
- Front/back side assignment: if a component's <code>TO.C</code> appears on F.Cu, it's front-side; if on B.Cu, it's back-side. Components appearing only on B.Cu are counted as back-only.
- Pad counts: maximum pad count per component across all layers (same pad appears on copper, mask, and paste layers)
- Nets per component: union of all nets associated with each component across layers

**Net classification** uses keyword matching:
- **Power nets**: Names matching <code>vcc</code>, <code>vdd</code>, <code>gnd</code>, <code>agnd</code>, <code>vss</code>, <code>vbat</code>, <code>vbus</code>, <code>vin</code>, or starting with <code>+</code>/<code>-</code>
- **Unnamed nets**: Starting with <code>Net-(</code> or <code>unconnected-(</code>
- **Signal nets**: Everything else

### 6.6 Net Analysis

<code>build_net_analysis()</code> merges net and pin data from copper layers only (mask/paste/silk layers don't carry meaningful net data).

Output includes:
- Total unique nets, named vs unnamed count
- Power and signal net lists (same classification as component analysis)
- Total pin-to-net mappings (deduplicated across layers)

### 6.7 Trace Width Analysis

<code>build_trace_analysis()</code> aggregates conductor aperture data from copper layers:
- **Unique widths**: Set of all trace widths used (from Conductor-tagged apertures)
- **Min/max trace**: Smallest and largest trace widths
- **Minimum feature**: Smallest aperture dimension of any type on copper layers

### 6.8 Pad Summary

<code>build_pad_summary()</code> counts pad types by aperture function across copper layers:

| Counter | Source |
|---|---|
| SMD apertures | <code>SMDPad</code> function on copper layers |
| Via apertures | <code>ViaPad</code> function on copper layers |
| Heatsink apertures | <code>HeatsinkPad</code> function on copper layers |
| THT holes | Component hole count from drill classification |

When both SMD and THT counts are available, an <code>smd_ratio</code> is computed (0.0 = all THT, 1.0 = all SMD).

### 6.9 Zip Archive Scanning

<code>scan_zip_archives()</code> detects <code>.zip</code> files in the Gerber directory and reports metadata to help identify stale archives or stale loose files. Gerber directories commonly contain zip archives — manufacturers require zipped uploads, and designers often snapshot Gerbers at different design stages.

**Per-archive data:**
- Filename, file size, filesystem modification time
- Total files inside, broken down by gerber/drill/other
- Newest member date (from the zip directory entries, not filesystem mtime)
- Comparison against loose Gerber files: <code>loose_files_newer</code>, <code>archive_newer</code>, or <code>same_age</code>
- Time delta in hours when ages differ (threshold: 60 seconds to ignore trivial filesystem jitter)

**Comparison logic:** The newest internal member date is preferred over the zip's filesystem mtime for comparison, since filesystem mtime can change from a copy/move without reflecting the actual export time. The loose file side uses the latest filesystem mtime across all gerber and drill files.

The analyzer does not extract or parse files from inside zip archives — it only inspects the zip directory. This is intentional: the loose files are what gets analyzed, and the zip scan exists to flag when those loose files may not match what was (or will be) uploaded to the manufacturer.

Only present in output when zip files exist in the directory.

---

## 7. Layer Count Determination

The total copper layer count is determined from the maximum of three sources:
1. **Parsed Gerber files**: Count of files identified as copper layers (<code>*.Cu</code>)
2. **Job file**: <code>GeneralSpecs.LayerNumber</code> from <code>.gbrjob</code>
3. **Drill layer span**: Maximum layer number from drill file <code>FileFunction</code> (e.g., <code>Plated,1,4</code> → 4 layers)

This handles cases where inner layer Gerbers might be missing or misidentified — the drill span and job file still report the correct count.

---

## 8. Output Structure

### Top-Level Keys

| Key | Type | Description |
|---|---|---|
| <code>directory</code> | string | Input directory path |
| <code>generator</code> | string\|null | CAD tool that produced the files |
| <code>layer_count</code> | int | Total copper layers |
| <code>board_dimensions</code> | object | Width, height, area, source |
| <code>statistics</code> | object | File counts, total holes/flashes/draws |
| <code>completeness</code> | object | Missing/extra layers, complete flag |
| <code>alignment</code> | object | Aligned flag, issues, per-layer extents |
| <code>drill_classification</code> | object | Vias/component/mounting holes with tools |
| <code>pad_summary</code> | object | SMD/via/heatsink/THT aperture counts |
| <code>trace_widths</code> | object | Width distribution, min feature (if available) |
| <code>component_analysis</code> | object | Component refs, front/back counts (X2 only) |
| <code>net_analysis</code> | object | Net counts, power/signal lists (X2 only) |
| <code>gerbers</code> | array | Per-file summary (compact) |
| <code>drills</code> | array | Per-file summary with tool details |
| <code>drill_tools</code> | object | Aggregated drill sizes and counts |
| <code>job_file</code> | object | Full <code>.gbrjob</code> metadata (if present) |
| <code>zip_archives</code> | array | Zip files with contents summary and staleness comparison (if present) |
| <code>connectivity</code> | array | Pin-to-net mappings (<code>--full</code> mode only) |

### Per-Gerber Summary

Each entry in the <code>gerbers</code> array contains:
- Filename, identified layer type, units
- Aperture count, flash/draw/region counts
- X2 file attributes (if present)
- Aperture analysis (function counts, conductor widths, min feature)
- X2 component/net/pin counts per layer (if present)

### Per-Drill Summary

Each entry in the <code>drills</code> array contains:
- Filename, PTH/NPTH type, units
- Total hole count
- Tool definitions with diameters and per-tool hole counts
- Layer span (if available from X2)
- X2 attributes

### Output Modes

- **Default**: Compact per-file summaries with board-level analysis
- **<code>--full</code>**: Adds raw pin-to-net connectivity data (every TO.P mapping)
- **<code>--compact</code>**: Minified JSON (no indentation)

---

## 9. Known Limitations

### Format Coverage

- **RS-274X only**: Does not parse legacy RS-274D (no embedded aperture definitions). RS-274D requires external aperture files — virtually all modern CAD tools export RS-274X.
- **Aperture macros**: Custom macro apertures (AM commands) beyond RoundRect are not dimension-parsed. They are still recorded as aperture definitions, but their dimensions don't contribute to trace width or min feature analysis.
- **Step-and-repeat**: SR commands (array replication) are not interpreted. The coordinate range will reflect the base pattern, not the replicated extent.
- **Block apertures**: AB (aperture block) commands are not interpreted.

### Coordinate Interpretation

- **Incremental mode**: The parser assumes absolute notation. Files using incremental coordinates (rare in modern output) will produce incorrect coordinate ranges.
- **Bounding box only**: Coordinate ranges are axis-aligned bounding boxes, not actual board geometry. Non-rectangular boards and cutouts are not detected.

### X2 Attribute Dependency

- Component analysis, net analysis, and pin connectivity **require X2 object attributes** (TO.C, TO.N, TO.P). These are only present in KiCad 6+ and other modern CAD exports that support the X2 extension.
- KiCad 5 exports include X2 file attributes (TF, via G04 comments) but not object attributes. For KiCad 5 Gerbers, the analyzer provides layer identification and statistics but no component or net data.

### Drill Interpretation

- **Routing commands**: G85 (routed slot), M15/M16 (routed drilling) are not interpreted. Routed slots are not counted as holes.
- **Multiple drill files**: Some exports split PTH and NPTH into separate files, others combine them. The analyzer handles both — it parses all <code>.drl</code> files and merges results. But if a combined file has no X2 attributes and no filename hint, it defaults to <code>"unknown"</code> type.

### Cross-Layer Analysis

- The analyzer does not perform geometric cross-referencing between layers (e.g., checking that a drill hole aligns with pads on copper layers, or that solder mask openings match pad sizes). These checks require spatial correlation that the Gerber format does not natively support without full coordinate parsing and matching.

---

## 10. Verification

The analyzer can be verified by:
1. **Round-trip comparison**: Run on Gerbers exported from a KiCad project, then compare component/net counts against the schematic and PCB analyses of the same project
2. **Job file cross-check**: Board dimensions and layer count from the analyzer should match <code>.gbrjob</code> values
3. **Completeness**: The completeness check itself verifies that the expected file list from <code>.gbrjob</code> matches what was found on disk
4. **Alignment**: Running on known-good Gerber sets should always report <code>"aligned": true</code>
