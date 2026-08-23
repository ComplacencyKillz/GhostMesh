# Datasheet v2 Schemas — CHANGELOG

Per-schema semver-lite versioning. Rules:

- **Additive within minor.** Adding an optional field → minor bump (0.3 → 0.4, 1.0 → 1.1). Consumers tolerate missing optional fields. Cached extractions remain valid.
- **Breaking = major bump.** Renaming, removing, or type-changing an existing field → major bump (0.3 → 1.0, 1.0 → 2.0). Cached extractions for that section flagged stale; re-extraction required for consumers gated on <code>min_schema >= new_major</code>.
- **Stale one section ≠ stale whole MPN.** A base→2.0 bump doesn't invalidate the regulator extension's cached values.
- **Pinout is flagged <code>still-calibrating</code>** through v1.4. The shape may shift with real-corpus feedback before v1.5 commits to strict additive-only discipline.

---

## crystal.schema.json v1.0 — 2026-04-27

Initial release. Phase 3b (extraction breadth). Final Phase 3b category. Fields cover AT-cut quartz crystals, ceramic resonators, TCXO, VCXO, OCXO. Field union from a single MVP MPN — ABM8G-106-12.000MHZ-T (Abracon 12 MHz AT-cut fundamental SMD crystal, 3.2×2.5×1.0 mm) — per harness Stage 0 coordination.

Required: <code>crystal_type</code> (enum: <code>at_cut | ceramic_resonator | tcxo | vcxo | ocxo</code>). All other fields nullable. Smallest Phase 3b schema (12 properties); crystals have less parameter spread than active ICs.

<code>motional_capacitance</code> and <code>motional_inductance</code> are typically published only for AT-cut quartz; ceramic resonators / TCXO / VCXO / OCXO usually omit them. Schema supports null for both.

<code>aging</code> uses plain <code>"ppm"</code> unit (no new unit needed). The first-year time window goes in the <code>condition</code> field (e.g. <code>"Aging @ 25°C ±3°C, first year"</code>). This aligns with the harness sanity vector, which stores aging as <code>{min: -3, max: 3, unit: "ppm"}</code> with a first-year condition note.

<code>mode</code> enum is <code>fundamental | overtone_3rd | overtone_5th</code> plus null. Most consumer crystals are fundamental-mode; overtone modes are common in low-noise oscillator applications. Null for ceramic resonators, TCXO, VCXO, OCXO (the term is not meaningful for integrated oscillators).

<code>package.body_mm</code> follows the diode/transistor/opamp/mcu convention: nested <code>{length, width, height}</code> (no <code>_mm</code> suffix on inner keys).

**Phase 3b deferred to v1.5:** The original brainstorm planned 2 MVP MPNs per category for field-union; crystal shipped with only 1 because the harness sanity-vector list (12 MPNs) had no crystal at brainstorm time. v1.5 will add a second crystal extraction (TCXO or low-frequency 32.768 kHz watch crystal preferred) to validate field-union coverage.

**No spec_value.schema.json amendments required.** Aging stores in <code>"ppm"</code> with first-year condition. No new units needed.

<code>thermal_resistance</code> is intentionally absent — crystals rarely publish thermal resistance figures. v1.5 may add it if TCXO/OCXO datasheets prove otherwise.

**Pin-resolution:** crystals have no pin-reference fields; <code>_PIN_FIELDS_BY_CATEGORY["crystal"]</code> is the empty tuple (already set in Task 2).

---

## mcu.schema.json v1.0 — 2026-04-27

Initial release. Phase 3b (extraction breadth). Catalog-tier-only extraction: identity-level facts (core, memory, peripheral counts, package, supply, debug interface, reset pin, temperature grades). Per-peripheral instance and pin-mux detail is explicitly deferred to Tier 2 (<code>mcu_peripherals.schema.json</code>, v1.5). Field union from ATmega328P-AU (Atmel/Microchip 8-bit AVR, TQFP-32, 32K flash) + STM32F103C8T6 (ST Cortex-M3 32-bit, LQFP-48, 64K flash).

Required: <code>core_family</code> (open string, not enum — accommodates future cores). All other fields nullable or defaultable.

**Plain-integer memory and frequency fields** (NOT SpecValue lists — these specs have no min/typ/max spread worth capturing at catalog tier): <code>core_speed_max</code> (Hz), <code>flash_size</code> (bytes), <code>ram_size</code> (bytes), <code>eeprom_size</code> (bytes), <code>pin_count</code>, <code>gpio_count</code>, <code>nvic_priorities</code>. SpecValue lists used only for supply voltages with real min/max ranges: <code>vdd_range</code>, <code>vddio_range</code>, <code>vdda_range</code>. This avoids spec_value unit enum sprawl (<code>bytes</code> and <code>Hz</code> prefix variants not added).

**peripheral_counts** is a closed object with required-int properties (<code>uart</code>, <code>spi</code>, <code>i2c</code>, <code>can</code>, <code>usb</code>, <code>ethernet</code>, <code>dac</code>, <code>timer_general</code>, <code>timer_advanced</code>, all <code>minimum: 0</code>). Convention: **0 for absent peripherals** (not null). When <code>peripheral_counts.dac = 0</code>, also set top-level <code>dac: null</code>. The prompt's hard rules document this exclusion explicitly. Per-instance peripheral configuration is Tier 2.

**adc/dac** are closed summary objects (bit_depth, channel_count, sample_rate_max_hz) capturing the part-level ADC/DAC characteristics. Null when the part has no ADC/DAC. Not SpecValue fields — these are structural counts, not spread specs.

**nvic_priorities** is null for non-Cortex-M cores (AVR, PIC, 8051, classic RISC-V without NVIC). For Cortex-M3: 16 (4-bit priority field). Documented in field description and prompt hard rule.

**eeprom_size convention:** use <code>0</code> for parts with no EEPROM (STM32F103C8T6 → 0); use actual byte count for parts with EEPROM (ATmega328P → 1024); null only when not determinable.

**debug_interface** is an enum string: <code>swd | jtag | swd_jtag | debugwire | pdi | spi_isp | none | null</code>. Covers the major debug/programming interfaces across AVR (debugwire), XMEGA (pdi), Cortex-M (swd, jtag, swd_jtag), and older ISP parts.

**boot_pins** is an array of <code>{pin_number, function}</code> objects or an empty array (AVR uses fuses, not boot pins) or null.

<code>reset_pin</code> matches <code>base.pinout[*].numbers</code> when populated. Pin-resolution registered as <code>_PIN_FIELDS_BY_CATEGORY["mcu"] = ("reset_pin",)</code> (from Task 2, already in verify_v14_extraction).

**Inherited conventions from diode/transistor/opamp:**
- <code>thermal_resistance</code>: nested object with <code>rtheta_ja</code>/<code>rtheta_jc</code>/<code>rtheta_jl</code> nullable SpecValue lists (K/W or °C/W).
- <code>package.body_mm</code>: nested <code>{length, width, height}</code> (no <code>_mm</code> suffix on inner fields; aligns with base.schema.json's pre-existing body_mm shape).

**No new spec_value.schema.json unit additions required.** Flash/RAM/EEPROM are plain integers; frequencies are plain integers; voltages use existing <code>"V"</code> unit. All 18 existing unit tokens remain unchanged.

**Field count: 22 properties** (well under the 35-property ceiling).

---

## opamp.schema.json v1.0 — 2026-04-27

Initial release. Phase 3b (extraction breadth). Fields cover general-purpose, precision, rail-to-rail I/O, rail-to-rail output, JFET-input, CMOS-input, chopper, instrumentation, and comparator op-amps. Field union from LM358 (Fairchild → ON Semi BJT general-purpose dual single-supply, SOIC-8/PDIP-8) + MCP6004 (Microchip CMOS rail-to-rail I/O quad, SOIC-14/PDIP-14/TSSOP-14).

Required: <code>opamp_topology</code> (enum) + <code>channels</code> (integer). All other fields nullable.

**Topology enum addition vs. design spec:** the design spec listed 8 topology values; v1.0 ships 9 — added <code>comparator</code> to cover open-collector comparator parts (LM393, LM339) which share op-amp IC families and packages. Treating comparators as an opamp-topology variant rather than a separate schema reduces Phase 3b/4 fragmentation; downstream consumers gate on the enum value.

<code>iq_per_amp</code> is per-channel quiescent current (NOT total Iq — divide by <code>channels</code> if the datasheet only gives total supply current ICC).

<code>shutdown_pin</code> matches <code>base.pinout[*].numbers</code> when populated. Both MVP MPNs lack a shutdown pin (<code>null</code> for both); the field exists for future shutdownable parts (e.g. MCP6N11, OPA376). Pin-resolution registered as <code>_PIN_FIELDS_BY_CATEGORY["opamp"] = ("shutdown_pin",)</code> (already from Task 2).

<code>vsupply_range</code> is the total spread (V+ to V−). LM358 spans 3–32V single = ±1.5V to ±16V split; the schema stores the single-supply equivalent with a condition note for the split-supply range.

<code>vout_swing_high</code> and <code>vout_swing_low</code> are stored as positive headroom-from-rail values (NOT absolute voltages). For rail-to-rail output parts, typical values are 25–50mV. For non-rail-to-rail parts (e.g. LM358), output swing high is 1.5–2V headroom from V+; output swing low is 5–20mV above V− at light load.

**v1.4 deferrals** (deferred to v1.5 to avoid spec_value unit enum sprawl):
- <code>noise_voltage_density</code> — would need <code>V/√Hz</code> unit
- <code>noise_current_density</code> — would need <code>A/√Hz</code> unit
- <code>phase_margin</code> — would need degrees unit
These are real op-amp parameters but their units don't fit the canonical SI enum pattern cleanly.

**spec_value.schema.json amendments (required by opamp field set):**
- Added <code>"V/s"</code> to <code>unit</code> enum — slew rate in V/s (NOT V/µs; store 0.6V/µs as <code>6e5</code>).
- Added <code>"dB"</code> to <code>unit</code> enum — CMRR, PSRR, open-loop gain.
Both additive non-breaking changes; existing cached extractions remain valid.

<code>thermal_resistance</code> follows the diode/transistor convention: <code>rtheta_ja</code>/<code>rtheta_jc</code>/<code>rtheta_jl</code> nullable SpecValue lists.

<code>package.body_mm</code> follows the diode/transistor convention: nested <code>{length, width, height}</code> (no <code>_mm</code> suffix on inner fields; aligns with base.schema.json's body_mm shape).

---

## transistor.schema.json v1.0 — 2026-04-27

Initial release. Phase 3b (extraction breadth). Fields cover BJT (NPN/PNP), MOSFET (N/P-channel), JFET, IGBT discrete transistors. Field union from 2N3904 (MCC NPN BJT, TO-92) + IRLML6344 (IR/Infineon N-MOSFET, SOT-23) datasheet review.

Required: <code>transistor_type</code> (enum). All other fields nullable. BJT-vs-FET encoded as null fields per inactive type (no <code>oneOf</code> discriminator) — schema accepts a part with all FET fields populated and all BJT fields null, or vice versa. The prompt's hard rules document this exclusion explicitly.

<code>pin_assignment</code> is a closed object with 6 nullable string fields (<code>base_pin</code>, <code>collector_pin</code>, <code>emitter_pin</code>, <code>gate_pin</code>, <code>drain_pin</code>, <code>source_pin</code>). For BJT/IGBT, populate base/collector/emitter; for FET/JFET, populate gate/drain/source. Pin-resolution against <code>base.pinout</code> is registered as the empty tuple in <code>_PIN_FIELDS_BY_CATEGORY["transistor"]</code> — the nested object's pin-string fields are not yet validated against pinout. v1.5 may add nested-object pin-resolution.

<code>hfe</code>, <code>id_max</code>, <code>power_dissipation</code>, <code>vce_sat</code>, <code>rds_on</code> accept multiple SpecValues per part disambiguated via condition string (different test currents, gate voltages, or temperatures).

<code>thermal_resistance</code> follows the diode-established convention: nested object with <code>rtheta_ja</code> / <code>rtheta_jc</code> / <code>rtheta_jl</code> nullable SpecValue lists (K/W or °C/W).

<code>package.body_mm</code> follows the diode-established convention: nested <code>{length, width, height}</code> (no <code>_mm</code> suffix on inner fields; aligns with base.schema.json's pre-existing shape).

**spec_value.schema.json amendments (required by transistor field set):**
- Added <code>null</code> to <code>unit</code> type (dimensionless quantities — <code>hfe</code> current gain has no SI unit).
- Added <code>"C"</code> (coulombs) to <code>unit</code> enum (gate charge fields <code>qg</code>, <code>qgd</code>).
Both are additive non-breaking changes; existing cached extractions remain valid.

---

## diode.schema.json v1.0 — 2026-04-27

Initial release. Phase 3b (extraction breadth). Fields cover signal, switching, Schottky, zener, TVS, rectifier, bridge, and varicap diodes. Field union from 1N4148 (Vishay signal switching, DO-35) + MBRS540T3G (ON Semi Schottky power, SMC) datasheet review.

Required: <code>diode_type</code> (enum). All other fields nullable to accommodate type-specific characteristics:
- <code>vz</code> only for zeners
- <code>trr</code> typically null for slow rectifiers/zeners
- <code>cd</code> most relevant for varicaps and signal/switching diodes
- <code>breakdown_voltage</code> separate from <code>vr_max</code> (1N4148 specs both at 100V; MBRS540T3G doesn't spec breakdown explicitly)

**Field-union additions vs. design-spec field list** (per Decision 10, schema covers union of both MVP MPNs' fields): <code>breakdown_voltage</code>, <code>tj_max</code>, and the third <code>thermal_resistance.rtheta_jl</code> sub-field (junction-to-lead) were added during datasheet read of 1N4148 + MBRS540T3G — they were not in the original spec field list but are real datasheet-published parameters worth capturing. Spec deviation is licensed by the field-union methodology.

<code>if_max</code> and <code>vr_max</code> accept multiple SpecValues per part disambiguated via condition string (continuous / average / repetitive peak; VRRM / VR / VRWM).

<code>thermal_resistance</code> is a nested object with three nullable sub-fields (<code>rtheta_ja</code>, <code>rtheta_jc</code>, <code>rtheta_jl</code> — all SpecValue lists, K/W). Through-hole parts typically have only <code>rtheta_ja</code>; SMD parts often have both <code>rtheta_jl</code> and <code>rtheta_ja</code>.

<code>package.body_mm</code> is a NESTED object <code>{length, width, height}</code> (Phase 3b option A convention — first new schema using this shape; inner field names align with <code>base.schema.json</code>'s pre-existing body_mm shape, no <code>_mm</code> suffix). Symmetric across categories (transistor/opamp/mcu/crystal will adopt same shape in subsequent tasks).

Pin-resolution: diodes have no pin-reference fields; <code>_PIN_FIELDS_BY_CATEGORY["diode"]</code> is the empty tuple (already set in Task 2).

---

## scout — 1.0 (2026-04-25, Phase 3a)

Fat-scout output: <code>{mpn, metadata, categories, extraction_pages, quality_verdict}</code>. Identifies datasheet characteristics, target category extensions, per-task page lists, and a quality verdict gating extraction dispatch.

## plan — 1.0 (2026-04-25, Phase 3a)

Orchestration plan written by <code>plan_extraction.py</code>, consumed by the dispatcher and <code>merge_results.py</code>. Shape: <code>{plan_version, mpn, pdf_path, pdf_sha256, cache_dir, scout_ref, tasks[], execution: {started_at, completed_at, outcomes[]}}</code>. Each task carries <code>task_id</code>, <code>subagent_role</code>, <code>tier</code>, <code>schema</code>, <code>prompt_template</code>, <code>pages</code>, <code>depends_on</code>, <code>status</code>, <code>result_ref</code>.

## base — 1.0 (2026-04-19, v1.4)

Initial version.

Shape: <code>{family, description, package, thermal, absolute_max, recommended_operating, esd, moisture_sensitivity, compliance, pinout, pin_relationships}</code>. <code>absolute_max</code>, <code>recommended_operating</code>, <code>esd</code>, and <code>thermal</code> are objects keyed by parameter name → <code>SpecValue[]</code> (see spec §4). <code>pinout</code> is a <code>$ref</code> to <code>pinout.schema.json</code>.

## pinout — 1.0 (2026-04-19, v1.4) — still-calibrating

Initial version. Pin shape: <code>{numbers[], name, type, subtype, description, power_domain, alt_functions[], is_5v_tolerant, absolute_max, recommended, drive_strength, notes, evidence}</code>. Type vocabulary mirrors KiCad ERC pin types.

Flagged <code>x-still-calibrating: true</code> — shape may shift before v1.5.

## spec_value — 1.0 (2026-04-19, v1.4)

Initial version. <code>{min, typ, max, unit, condition, notes, evidence: {page, section, confidence, method}}</code>. Canonical SI units only. Always serialized inside a list (one-element list for single-value specs).

## regulator — 0.3 (2026-04-19, v1.4)

Initial version. Category extension for voltage regulators. Flat topology enum per spec §7 (<code>ldo|buck|boost|buck_boost|sepic|flyback|charge_pump|isolated</code>). Optional SpecValue[] fields for vin_range, vout_range, iout_max, reference_voltage, cin_min, cout_min, inductor_range, switching_freq, dropout, psrr, line_regulation, load_regulation. <code>stability_conditions</code> + <code>sequencing</code> nested objects feed SV-001 and ST-001.

Version 0.3 (not 1.0) because the field set may still shift — the first real extractions against diverse parts (LDO vs buck vs boost) may prompt adjustments. Promoted to 1.0 once v1.4 corpus re-extraction validates the shape.

## extraction — 1.0 (2026-04-19, v1.4)

Initial version. Top-level per-MPN file envelope: <code>{schema_version, source, extraction, base, categories, <per-category>}</code>. <code>source.family_ref</code> and <code>source.sha256</code> enable Tier 1 dedup; <code>categories[]</code> lists the active extensions.

## manifest — 1.0 (2026-04-19, v1.4)

Initial version. <code>datasheets/manifest.json</code> shape covering both legacy <code>extractions</code> index (v1.3 compat) and new <code>pdfs</code> SHA-dedup section (Tier 1).
