# Analyzer JSON Output Schema

**Generated from envelope dataclasses. Do not hand-edit.**
Regenerate: <code>python3 skills/kicad/scripts/gen_output_schema_md.py</code>

Source-of-truth modules:
- <code>skills/kicad/scripts/analyzer_envelope.py</code> — shared primitives (TrustSummary, Finding, BySeverity, ...)
- <code>skills/kicad/scripts/envelopes/*.py</code> — per-analyzer envelopes (schematic, pcb, gerber, thermal, cross_analysis)
- <code>skills/emc/scripts/emc_envelope.py</code> — EMC envelope

For the authoritative machine-readable JSON Schema Draft 2020-12, use <code>--schema</code> on any analyzer:

```bash
python3 skills/kicad/scripts/analyze_schematic.py --schema
python3 skills/kicad/scripts/analyze_pcb.py --schema
python3 skills/kicad/scripts/analyze_gerbers.py --schema
python3 skills/kicad/scripts/analyze_thermal.py --schema
python3 skills/emc/scripts/analyze_emc.py --schema
python3 skills/kicad/scripts/cross_analysis.py --schema
```

v1.4 schema-break notes:
- Output of <code>--schema</code> is real JSON Schema Draft 2020-12 (prior: descriptive-string dict).
- <code>schema_version</code> bumped 1.3.0 → 1.4.0 on every analyzer.
- <code>trust_summary.by_confidence</code> aggregate key renamed: <code>datasheet-backed</code> → <code>datasheet_backed</code>. Per-finding <code>confidence</code> VALUE stays <code>datasheet-backed</code>.

## Contract Tiers

Every analyzer envelope is organized into three tiers. Consumers can
rely on Tier 1 shapes; Tier 2 is analyzer-specific and best read via
the declared dataclass types; Tier 3 is compatibility residue slated
for removal and should not be written against in new code.

### Tier 1 — Standardized envelope (stable across v1.4)

Present on every analyzer output. Shape locked by the shared primitives
in <code>analyzer_envelope.py</code>. Breaking changes bump the analyzer's
<code>schema_version</code>.

- <code>analyzer_type</code> — <code>const</code> string discriminator naming the analyzer.
- <code>schema_version</code> — <code>const</code> string matching the semver.
- <code>summary</code> — per-analyzer roll-up (<code>total_findings</code>, <code>by_severity</code>,
  analyzer-specific counts). Inner shape is analyzer-specific but the
  top-level key is Tier 1.
- <code>trust_summary</code> — trust posture: <code>total_findings</code>, <code>trust_level</code>,
  <code>by_confidence</code>, <code>by_evidence_source</code>, <code>provenance_coverage_pct</code>,
  <code>bom_coverage</code> (schematic only).
- <code>findings</code> — <code>list[Finding]</code>. Actionable items with severity +
  recommendation.
- <code>assessments</code> — <code>list[Assessment]</code>. Informational measurements (no
  severity, no recommendation). Empty on analyzers with no assessment
  content today.
- <code>inputs</code> — <code>InputsBlock</code>. <code>source_files</code>, <code>source_hashes</code>, <code>run_id</code>,
  <code>config_hash</code>, <code>upstream_artifacts</code>.
- <code>compat</code> — <code>CompatBlock</code>. <code>minimum_consumer_version</code>,
  <code>deprecated_fields</code>, <code>experimental_fields</code>.

### Tier 2 — Analyzer-specific body

Everything emitted by a given analyzer that is not listed in Tier 1.
Shape is declared by the per-analyzer envelope in <code>envelopes/*.py</code> or
<code>emc_envelope.py</code>. Typical Tier 2 keys include <code>statistics</code>,
<code>components</code>, <code>nets</code>, <code>bom</code>, <code>ic_pin_analysis</code>, <code>design_analysis</code>,
<code>bus_topology</code>, <code>placement_analysis</code>, <code>power_net_routing</code>,
<code>connectivity_graph</code>, EMC <code>test_plan</code> / <code>regulatory_coverage</code>, thermal
<code>thermal_score</code>, gerber <code>layers</code> / <code>drills</code> / <code>completeness</code>, etc.

Several Tier 2 fields are currently typed as loose <code>dict</code> or
<code>list[dict]</code> with <code>TODO(v1.5)</code> markers. Consumers that need stable
shapes from these should wait for the v1.5 per-rule_id tightening pass.

### Tier 3 — Compatibility residue

Empty for v1.4. The v1.4 clean break removed prior residue:

- <code>schematic.file</code>, <code>pcb.file</code> — removed; use <code>inputs.source_files[0]</code>.
- <code>thermal.thermal_assessments</code> — renamed to <code>thermal.assessments</code>
  (sibling to <code>findings</code>, not inside it).
- Descriptive-string <code>--schema</code> output — replaced by real JSON Schema
  Draft 2020-12.
- <code>trust_summary.by_confidence.datasheet-backed</code> key — renamed to
  <code>datasheet_backed</code> (hyphen removed only for the aggregate-count key;
  the per-finding <code>confidence</code> VALUE still uses <code>datasheet-backed</code>).
- Deprecated <code>summary.critical</code> / <code>.high</code> / <code>.medium</code> / <code>.low</code> / <code>.info</code>
  keys on thermal — removed in v1.4.

## SchematicEnvelope

Output of <code>python3 skills/kicad/scripts/analyze_schematic.py <file>.kicad_sch</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'schematic'. |
| <code>schema_version</code> | <code>string</code> | yes | Semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source files, hashes, run_id, config_hash, upstream artifacts for this run. |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>summary</code> | <code>SchematicSummary</code> | yes | Roll-up summary (total + by_severity). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup (confidence + evidence source + optional BOM coverage). |
| <code>kicad_version</code> | <code>string</code> | yes | KiCad generator version string, e.g. '9.0' or '5 (legacy)'. |
| <code>file_version</code> | <code>string</code> | yes | KiCad file format version string. |
| <code>title_block</code> | <code>TitleBlock</code> | yes | KiCad title block (title/date/rev/company/comments). |
| <code>statistics</code> | <code>Statistics</code> | yes | Component, net, and coverage counts. |
| <code>findings</code> | <code>list[Finding]</code> | yes | All findings (flat, rich-finding format). |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | Informational assessments (empty for schematic at v1.4; reserved for future measurement-style records). |
| <code>bom</code> | <code>list[BomEntry]</code> | yes | Deduplicated BOM rows. |
| <code>components</code> | <code>list[dict]</code> | yes | Every non-power component as a dict. Shape is effectively open: reference/value/lib_id/footprint/datasheet/description/mpn/manufacturer/distributor SKUs/geometry/uuid/type/parsed_value plus internal bookkeeping (_sheet, pin_nets, pin_uuids). Tightens to a typed Component in v1.5. |
| <code>nets</code> | <code>dict[str, NetEntry]</code> | yes | Net connectivity map keyed by unique net key — the display name, sheet-qualified as /<sheet>/<name> when distinct nets share a bare name. |
| <code>subcircuits</code> | <code>list[dict]</code> | yes | Hierarchical sub-sheets: [{reference, path, sheet_name, sheet_file, instances}]. |
| <code>ic_pin_analysis</code> | <code>list[dict]</code> | yes | Per-IC pin mappings. Each entry carries reference, value, type, lib_id, mpn, description, datasheet, function, total_pins, unconnected_pins, pins[], power_pins[], signal_pins[], decoupling_caps_by_rail. Covers type in {ic, connector, crystal, oscillator}; transistors live in transistor_pin_analysis[] (F4). |
| <code>transistor_pin_analysis</code> | <code>list[dict]</code> | yes | Per-transistor pin mappings. Same shape as ic_pin_analysis entries but filtered to type=transistor (MOSFETs, BJTs, FETs). Lets bridge / half-bridge / gate-driver reviewers verify gate/source/drain wiring without reconstructing pin maps from nets[].pins[] by hand. F4. |
| <code>design_analysis</code> | <code>dict</code> | yes | Design-level analyses: net_classification, power_domains, cross_domain_signals, bus_analysis (i2c/spi/uart/can), differential_pairs, erc_warnings, passive_warnings. |
| <code>connectivity_issues</code> | <code>dict</code> | yes | Connectivity issue lists: single_pin_nets, single_pin_net_findings, multi_driver_nets, unconnected_pins, power_net_summary. |
| <code>annotation_issues</code> | <code>dict</code> | yes | Annotation issue bag: duplicate_references, unannotated, missing_value, zero_indexed_refs. |
| <code>ground_domains</code> | <code>dict</code> | yes | Ground topology: ground_nets, multiple_domains, domains, optional star-ground note. |
| <code>bus_topology</code> | <code>BusTopology</code> | yes | Bus wire statistics: bus_wire_count, bus_entry_count, unresolved. May also carry aliases / detected_bus_signals (undeclared, shape varies). |
| <code>wire_geometry</code> | <code>dict</code> | yes | Wire-geometry summary: total_wires, total_length_mm, avg_length_mm, optional diagonal/short-wire callouts. |
| <code>simulation_readiness</code> | <code>dict</code> | yes | SPICE readiness rollup: total_components, likely_simulatable, needs_model, simulatable_percent, components_without_model. |
| <code>hierarchical_labels</code> | <code>dict</code> | yes | Label counts: global_label_count, hierarchical_label_count, optional unconnected_hierarchical or conflict warnings. |
| <code>placement_analysis</code> | <code>dict</code> | yes | Placement stats: bounding_box, clusters, grid_size. |
| <code>property_issues</code> | <code>dict</code> | yes | Property validation issues: missing/mismatched symbol properties (MPN, footprint, datasheet, value), blank description fields, non-ASCII character warnings. Keyed by issue category. |
| <code>sourcing_audit</code> | <code>dict</code> | yes | Sourcing audit: missing_mpn/digikey/lcsc lists, mpn_coverage, mpn_percent, total_bom_components. |
| <code>rail_voltages</code> | <code>dict[str, float \| null]</code> | yes | Per-net rail voltage (volts). Nulls permitted for ground or unresolved rails. |
| <code>labels</code> | <code>list[dict]</code> | no | Extracted label records. |
| <code>no_connects</code> | <code>list[dict]</code> | no | Extracted no-connect flag records. |
| <code>power_symbols</code> | <code>list[dict]</code> | no | Power symbol placements: [{net_name, x, y, lib_id, _sheet, _power_scope}]. |
| <code>pwr_flag_warnings</code> | <code>list[dict]</code> | no | PWR_FLAG warnings: [{net, message, pin_types}]. |
| <code>label_shape_warnings</code> | <code>list</code> | no | Label-shape mismatch warnings. |
| <code>footprint_filter_warnings</code> | <code>list</code> | no | Footprint filter warnings from lib_symbols. |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |
| <code>audience_summary</code> | <code>dict \| null</code> | no | Designer/reviewer/manager summary views; only present when output filters ran. |
| <code>design_intent</code> | <code>dict \| null</code> | no | Resolved design intent: approved_manufacturers, ipc_class, target_market, operating_temp_range, preferred_passive_size, product_class, test_coverage_target, expected_lifetime_years, detection_signals, source, confidence. |
| <code>project_config</code> | <code>dict \| null</code> | no | Copy of the resolved project block from .kicad-happy.json (when present). |
| <code>project_settings</code> | <code>dict \| null</code> | no | Selected KiCad project settings extracted from .kicad_pro (when present). |
| <code>bom_lock</code> | <code>dict \| null</code> | no | BOM lock verification: status, lock_pct, components_with_mpn, missing_mpn, etc. |
| <code>bom_optimization</code> | <code>dict \| null</code> | no | BOM optimization bag: single_use_passive_values, unique_value_counts, total_unique_footprints, consolidation_suggestions. |
| <code>missing_info</code> | <code>dict \| null</code> | no | Data-gap rollup: missing_mpn, missing_footprint, missing_datasheet, heuristic_vref. |
| <code>sleep_current_audit</code> | <code>dict \| null</code> | no | Sleep-current audit: rails, total_estimated_sleep_uA, realistic_total_uA, conditional_pull_up_uA, observations. |
| <code>test_coverage</code> | <code>dict \| null</code> | no | Test coverage audit: test_points, test_point_count, covered_nets, uncovered_key_nets, observations, optional debug_connectors. |
| <code>assembly_complexity</code> | <code>dict \| null</code> | no | Assembly complexity score and breakdown. |
| <code>power_budget</code> | <code>dict \| null</code> | no | Estimated power budget per rail. |
| <code>power_sequencing</code> | <code>dict \| null</code> | no | Power sequencing dependencies and warnings. |
| <code>power_sequencing_validation</code> | <code>dict \| null</code> | no | Validated power tree with provenance. |
| <code>pdn_impedance</code> | <code>dict \| null</code> | no | PDN impedance analysis per rail. |
| <code>usb_compliance</code> | <code>dict \| null</code> | no | USB compliance checks. |
| <code>inrush_analysis</code> | <code>dict \| null</code> | no | Inrush current estimation by rail. |
| <code>protocol_compliance</code> | <code>dict \| null</code> | no | Protocol compliance checks (I2C/SPI/UART/etc.). |
| <code>text_annotations</code> | <code>list \| null</code> | no | Text annotations extracted from the schematic. |
| <code>alternate_pin_summary</code> | <code>dict \| null</code> | no | Alternate-pin usage summary. |
| <code>pin_coverage_warnings</code> | <code>list[PinCoverageWarning] \| null</code> | no | KH-323: warnings about pin coverage gaps. Emitted when a placed symbol has fewer pins connected than the library definition expects. Present only when such warnings fire. |
| <code>instance_consistency_warnings</code> | <code>list \| null</code> | no | Multi-instance symbol consistency warnings. |
| <code>generic_symbol_warnings</code> | <code>list \| null</code> | no | Warnings about generic symbol usage without MPN. |
| <code>sheets</code> | <code>list \| null</code> | no | Hierarchical sheet list (multi-sheet only). |
| <code>sheets_parsed</code> | <code>int \| null</code> | no | Count of parsed sheets (legacy .sch). |
| <code>sheet_files</code> | <code>list[string] \| null</code> | no | Parsed sheet file paths (legacy .sch). |
| <code>legacy_analysis_quality</code> | <code>dict \| null</code> | no | Legacy .sch quality rollup: is_legacy_schematic, library_resolution, pin_source_coverage. |
| <code>hierarchy_context</code> | <code>dict \| null</code> | no | Hierarchy context (sub-sheet analysis): root_schematic, target_sheet, sheets_in_project, cross_sheet_nets, project_power_rails, reference_corrections_applied. |
| <code>hierarchy_warning</code> | <code>string \| null</code> | no | Emitted when sub-sheet was detected without root. |
| <code>_redirected_from</code> | <code>string \| null</code> | no | Original filename when a sub-sheet was redirected to the project root. Emitted in JSON as '_redirected_from'. |
| <code>_stale_file_warning</code> | <code>string \| null</code> | no | Emitted when input file is not in the project sheet tree. Emitted in JSON as '_stale_file_warning'. |
| <code>net_classifications</code> | <code>dict \| null</code> | no | Per-net classification map promoted from signal_analysis (legacy alias of design_analysis.net_classification). |

## PCBEnvelope

Output of <code>python3 skills/kicad/scripts/analyze_pcb.py <file>.kicad_pcb</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'pcb'. |
| <code>schema_version</code> | <code>string</code> | yes | Semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source files, hashes, run_id, config_hash, upstream artifacts for this run. |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>summary</code> | <code>PCBSummary</code> | yes | Roll-up summary (total + by_severity). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup (confidence + evidence source). |
| <code>kicad_version</code> | <code>string</code> | yes | KiCad generator version string, e.g. '9.0'. |
| <code>file_version</code> | <code>string</code> | yes | KiCad file format version string (e.g. '20241228'). |
| <code>findings</code> | <code>list[Finding]</code> | yes | All PCB findings (flat list). |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | Informational assessments (empty for PCB at v1.4). |
| <code>statistics</code> | <code>PCBStatistics</code> | yes | Board/component/net counts and routing rollup. |
| <code>setup</code> | <code>PCBSetup</code> | yes | Board setup block (thickness, soldermask, etc.). |
| <code>board_outline</code> | <code>BoardOutline</code> | yes | Edge.Cuts outline geometry + bounding box. |
| <code>connectivity</code> | <code>Connectivity</code> | yes | Routing completeness rollup. |
| <code>tracks</code> | <code>Tracks</code> | yes | Track summary + optional detailed arrays under --full. |
| <code>vias</code> | <code>Vias</code> | yes | Via summary + optional detailed array under --full. |
| <code>nets</code> | <code>dict[str, string]</code> | yes | Net ID (as string) -> net name. |
| <code>net_name_to_id</code> | <code>dict[str, int]</code> | yes | Net name -> integer net ID. Reverse of nets. |
| <code>layers</code> | <code>list[dict]</code> | yes | Layer stackup entries: [{number, name, type, alias}]. |
| <code>footprints</code> | <code>list[dict]</code> | yes | Every placed footprint. Fields include reference, value, library, footprint, layer, x, y, angle, type, mpn, manufacturer, description, pad_count, courtyard, courtyard_poly, pad_nets, connected_nets, sch_path, sheetname, sheetfile. Tightens to typed Footprint in v1.5. |
| <code>zones</code> | <code>list[dict]</code> | yes | Copper zones: [{net, net_name, layers, clearance, min_thickness, thermal_gap, thermal_bridge_width, outline_points, outline_area_mm2, is_filled, outline_bbox}]. |
| <code>keepout_zones</code> | <code>list[dict]</code> | yes | Keepout zones: [{name, layers, restrictions, bounding_box, area_mm2, nearby_components}]. |
| <code>net_classes</code> | <code>list[dict]</code> | yes | Net class definitions: [{name, clearance, track_width, via_diameter, via_drill}]. |
| <code>net_lengths</code> | <code>list[dict]</code> | yes | Per-net track length rollup: [{net, net_number, total_length_mm, segment_count, via_count, layers}]. |
| <code>component_groups</code> | <code>dict[str, ComponentGroup]</code> | yes | Refdes prefix -> {count, references}. |
| <code>silkscreen</code> | <code>dict</code> | yes | Silkscreen rollup: board_text_count, refs_visible_on_silk, refs_hidden_on_silk, documentation_warnings[], fab_notes_completeness, silkscreen_completeness. |
| <code>dfm_summary</code> | <code>dict</code> | yes | DFM rollup: dfm_tier, metrics, violation_count. |
| <code>project_settings</code> | <code>dict</code> | yes | Selected settings extracted from .kicad_pro: source, net_classes, design_rules. |
| <code>design_rule_compliance</code> | <code>dict</code> | no | Design rule compliance: compliant, rules_checked, rules_source; empty dict when project_settings missing or compliance not computed. TH-043. |
| <code>board_thickness_mm</code> | <code>float \| null</code> | no | Stackup thickness (mm); duplicated from setup for downstream consumers. Null when the source file has no (general (thickness ...)) entry. TH-043. |
| <code>board_metadata</code> | <code>dict</code> | no | Board metadata bag (paper size, title block fragments, etc.); empty dict when no metadata extracted. TH-043. |
| <code>power_net_routing</code> | <code>list[dict]</code> | no | Power net routing rollup: [{net, track_count, total_length_mm, min_width_mm, max_width_mm, widths_used}]; empty list when no power routing detected. TH-043-residual. |
| <code>ground_domains</code> | <code>dict</code> | no | Ground topology: domain_count, domains[], multi_domain_components. Always emitted; domain_count=0 is meaningful (no ground domain found). TH-043-residual. |
| <code>placement_density</code> | <code>dict</code> | no | Placement density: board_area_cm2, front_density_per_cm2, optional back_density_per_cm2; empty dict when density not computed. TH-043-residual. |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |
| <code>audience_summary</code> | <code>dict \| null</code> | no | Designer/reviewer/manager summary views; only present when output filters ran. |
| <code>design_intent</code> | <code>dict \| null</code> | no | Resolved design intent: product_class, ipc_class, target_market, operating_temp_range, preferred_passive_size, test_coverage_target, approved_manufacturers, expected_lifetime_years, detection_signals, confidence, source. |
| <code>project_config</code> | <code>dict \| null</code> | no | Copy of the resolved project block from .kicad-happy.json (when present). |
| <code>connectivity_graph</code> | <code>dict \| null</code> | no | Per-net connectivity graph (island map). Emitted only in --full mode. |
| <code>pad_to_pad_distances</code> | <code>dict \| null</code> | no | Pad-to-pad routing distances keyed by 'R1.2-D1.1' style endpoints. Emitted only in --full mode. |
| <code>thermal_analysis</code> | <code>dict \| null</code> | no | Thermal management analysis (when triggered). |
| <code>thermal_pad_vias</code> | <code>dict \| null</code> | no | Thermal pad via audit (when triggered). |
| <code>trace_proximity</code> | <code>dict \| null</code> | no | Trace proximity / crosstalk analysis (--proximity). |
| <code>copper_presence</code> | <code>dict \| null</code> | no | Copper presence sampling rollup (when triggered). |
| <code>tombstoning_risk</code> | <code>dict \| null</code> | no | Tombstoning risk analysis (when triggered). |
| <code>decoupling_placement</code> | <code>dict \| null</code> | no | Decoupling capacitor placement audit (when triggered). |
| <code>current_capacity</code> | <code>dict \| null</code> | no | Current capacity analysis (when triggered). |
| <code>placement_analysis</code> | <code>dict \| null</code> | no | Placement analysis details (when triggered). |
| <code>dfm</code> | <code>dict \| null</code> | no | Extended DFM analysis (when triggered). |

## GerberEnvelope

Output of <code>python3 skills/kicad/scripts/analyze_gerbers.py <gerber_dir>/</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'gerber'. |
| <code>schema_version</code> | <code>string</code> | yes | Semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source files, hashes, run_id, config_hash, upstream artifacts for this run. |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>summary</code> | <code>GerberSummary</code> | yes | Roll-up summary (total + by_severity). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup (confidence + evidence source). |
| <code>directory</code> | <code>string</code> | yes | Resolved absolute path of the scanned gerber directory. |
| <code>layer_count</code> | <code>int</code> | yes | Layer count derived from gbrjob or filename heuristic. |
| <code>statistics</code> | <code>GerberStatistics</code> | yes | File / draw / flash / hole totals. |
| <code>completeness</code> | <code>Completeness</code> | yes | Expected-vs-found layers and drill presence. |
| <code>alignment</code> | <code>Alignment</code> | yes | Cross-layer alignment report. |
| <code>drill_classification</code> | <code>DrillClassification</code> | yes | Vias / component / mounting hole breakdown. |
| <code>pad_summary</code> | <code>PadSummary</code> | yes | Aperture-function rollup (SMD / via / TH / heatsink). |
| <code>findings</code> | <code>list[Finding]</code> | yes | All gerber findings (flat list). |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | Informational assessments (empty for gerber at v1.4). |
| <code>board_dimensions</code> | <code>BoardDimensions</code> | yes | Physical board dimensions (from gbrjob or edge cuts). |
| <code>generator</code> | <code>string \| null</code> | no | Generator string (e.g. 'Pcbnew 10.0.1-...'); null if no GenerationSoftware tag and no gbrjob info. |
| <code>gerbers</code> | <code>list[dict]</code> | no | Per-gerber-file summary. Each item: {filename, layer_type, units, aperture_count, draw_count, flash_count, region_count, x2_attributes, x2_component_count, x2_net_count, x2_pin_count, aperture_analysis}. |
| <code>drills</code> | <code>list[dict]</code> | no | Per-drill-file summary. Each item: {filename, type, units, hole_count, layer_span, tools (T# -> {diameter_mm, hole_count, aper_function}), x2_attributes}. |
| <code>drill_tools</code> | <code>dict[str, dict]</code> | no | Aggregated tool map keyed by 'Xmm' diameter label with value {diameter_mm, count, type}. |
| <code>trace_widths</code> | <code>TraceWidths \| null</code> | no | Trace width rollup; omitted when no conductor apertures were observed. |
| <code>component_analysis</code> | <code>ComponentAnalysis \| null</code> | no | X2 component attribute rollup; omitted when no X2 component data in the gerber stream. |
| <code>net_analysis</code> | <code>NetAnalysis \| null</code> | no | X2 net attribute rollup; omitted when no X2 net data in the gerber stream. |
| <code>job_file</code> | <code>dict \| null</code> | no | Parsed .gbrjob contents: project_name, vendor, generator, layer_count, board_width_mm, board_height_mm, board_thickness_mm, creation_date, finish, stackup, design_rules, expected_files. Omitted when no .gbrjob present. |
| <code>zip_archives</code> | <code>list[dict] \| null</code> | no | Zip archive sweep; each entry: {filename, size_bytes, modified, total_files, gerber_files, drill_files, other_files, newest_member_date, staleness_warning}. Omitted when no .zip files in the directory. |
| <code>connectivity</code> | <code>list[dict] \| null</code> | no | Flat pin-to-net list from X2 attributes (--full only). Each item: {ref, pin, pin_name, net}. |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |
| <code>audience_summary</code> | <code>dict \| null</code> | no | Designer/reviewer/manager summary views; only present when output filters ran. |

## ThermalEnvelope

Output of <code>python3 skills/kicad/scripts/analyze_thermal.py --schematic ... --pcb ...</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'thermal'. |
| <code>schema_version</code> | <code>string</code> | yes | Schema semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source JSON inputs, sha256s, run_id, plus upstream artifact metadata (schematic, pcb). |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>summary</code> | <code>ThermalSummary</code> | yes | Roll-up summary of thermal analysis. |
| <code>findings</code> | <code>list[Finding]</code> | yes | All thermal findings: TS-001..005, TP-001..002. |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | TH-DET entries — per-component junction-temperature estimates. Informational (not findings). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup. |
| <code>elapsed_s</code> | <code>float</code> | yes | Wall-clock analysis time in seconds. |
| <code>missing_info</code> | <code>ThermalMissingInfo \| null</code> | no | Emitted when any component used default thermal params. |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |

## EMCEnvelope

Output of <code>python3 skills/emc/scripts/analyze_emc.py --schematic ... --pcb ...</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'emc'. |
| <code>schema_version</code> | <code>string</code> | yes | Semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source JSON inputs, sha256s, run_id, plus upstream artifact metadata (schematic, pcb). |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>target_standard</code> | <code>string</code> | yes | Target EMC standard key (e.g. 'fcc-class-b', 'cispr-class-b', 'cispr-25'). |
| <code>summary</code> | <code>EMCSummary</code> | yes | EMC roll-up summary (counts + risk score). |
| <code>findings</code> | <code>list[Finding]</code> | yes | All EMC findings. |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | Informational assessments (empty for EMC at v1.4). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup (confidence + evidence source). |
| <code>elapsed_s</code> | <code>float</code> | yes | Analysis wall-clock time in seconds. |
| <code>per_net_scores</code> | <code>list[PerNetScore]</code> | yes | Per-net EMC risk score rollup, sorted worst-first. |
| <code>test_plan</code> | <code>TestPlan</code> | yes | Pre-compliance test plan: frequency band priority, interface risk ranking, probe point suggestions. |
| <code>regulatory_coverage</code> | <code>RegulatoryCoverage</code> | yes | Coverage matrix vs. applicable standards for the target market. |
| <code>category_summary</code> | <code>dict[str, CategorySummaryEntry]</code> | yes | Category label -> {count, max_severity, severities, suppressed_count}. |
| <code>board_info</code> | <code>BoardInfo</code> | yes | Board-level rollup (dimensions, layer count, crystal + switching frequencies, ...). |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |
| <code>audience_summary</code> | <code>dict \| null</code> | no | Designer/reviewer/manager summary views. Present whenever findings[] is non-empty (the analyzer always builds this when findings exist). |
| <code>stage_filter</code> | <code>dict \| null</code> | no | Stage-filtered findings rollup. Present only when --stage is passed. |

## CrossAnalysisEnvelope

Output of <code>python3 skills/kicad/scripts/cross_analysis.py --schematic ... --pcb ...</code>.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| <code>analyzer_type</code> | <code>string</code> | yes | Always 'cross_analysis'. |
| <code>schema_version</code> | <code>string</code> | yes | Semver. Value: '1.4.0' at Track 1.1 landing. |
| <code>inputs</code> | <code>InputsBlock</code> | yes | Source JSON inputs, sha256s, run_id, plus upstream artifact metadata (schematic, pcb). |
| <code>compat</code> | <code>CompatBlock</code> | yes | Schema compatibility metadata: minimum consumer version + deprecated/experimental field lists. |
| <code>elapsed_s</code> | <code>float</code> | yes | Analysis wall-clock time in seconds. |
| <code>summary</code> | <code>CrossAnalysisSummary</code> | yes | Roll-up summary (total + by_severity). |
| <code>findings</code> | <code>list[Finding]</code> | yes | All cross-domain findings. |
| <code>assessments</code> | <code>list[Assessment]</code> | yes | Informational assessments (empty for cross-analysis at v1.4). |
| <code>trust_summary</code> | <code>TrustSummary</code> | yes | Trust posture rollup (confidence + evidence source). |
| <code>capability_mode_ref</code> | <code>dict \| null</code> | no | Pointer to canonical analysis/capability_mode.json run-level record. Shape: {source, run_id}. See Phase 4 spec §3.3. |
| <code>audience_summary</code> | <code>dict \| null</code> | no | Designer/reviewer/manager summary views. Added by apply_output_filters whenever findings[] is non-empty. |
| <code>stage_filter</code> | <code>dict \| null</code> | no | Stage-filtered findings rollup. Present only when --stage is passed. |
