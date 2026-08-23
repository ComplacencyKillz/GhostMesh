# Field Extraction Guide

How to find each schema field in a typical datasheet PDF. Covers section naming conventions by vendor, what language to look for, and common mistakes.

The extraction workflow starts with <code>datasheet_page_selector.py</code> identifying the relevant pages (pin table, absolute maximum ratings, operating conditions, electrical characteristics, application circuit). This guide describes what to look for once you have those pages.

Use <code>null</code> for any field the datasheet does not specify. Do not guess or interpolate.

---

## Page Selection Overview

The page selector uses a three-strategy approach:

1. **TOC present** — scans the first 1–3 pages for section headings with page numbers. TOC references to "Pin Configuration", "Absolute Maximum Ratings", "Electrical Characteristics", and "Typical Application" are automatically resolved to target pages.
2. **No TOC** — scores every page by keyword density. Pages containing "absolute maximum", "pin configuration", "electrical characteristics", and "application circuit" score highest.
3. **No pdftotext** — returns pages 1–5 plus evenly distributed pages.

Default page budget: 10 pages (15 for microcontrollers, FPGAs, SoCs). Always includes page 1 and the last page.

---

## Vendor Section Naming

Different manufacturers use different section titles for the same content. Recognizing these saves time.

### Texas Instruments (TI)

| Content | Typical Section |
|---------|----------------|
| Pin table | §6 "Pin Configuration and Functions" or §7 |
| Absolute maximum ratings | §7.1 "Absolute Maximum Ratings" |
| Operating conditions | §7.2 "ESD Ratings" / §7.3 "Recommended Operating Conditions" |
| Electrical characteristics | §7.4 or §7.5 "Electrical Characteristics" |
| Application circuit | §9 "Application and Implementation" → §9.1 "Typical Application" |

### STMicroelectronics (ST)

| Content | Typical Section |
|---------|----------------|
| Pin table | §4 "Pinouts" or "Pin definition" |
| Absolute maximum ratings | §5 or §6 "Absolute maximum ratings" |
| Operating conditions | Embedded in electrical characteristics table |
| Electrical characteristics | §6 or §7 "Electrical characteristics" |
| Application circuit | "Application schematics" or "Reference circuit" (late in doc) |

### NXP / Freescale

| Content | Typical Section |
|---------|----------------|
| Pin table | §7 "Pinning information" |
| Absolute maximum ratings | §11 "Limiting values" or "Absolute maximum ratings" |
| Operating conditions | §12 "Recommended operating conditions" or "Characteristics" |
| Electrical characteristics | §12 "Characteristics" |
| Application circuit | "Application diagram" or "Typical application circuit" |

### Microchip / Atmel

| Content | Typical Section |
|---------|----------------|
| Pin table | "Pin Diagrams" + "Pin Description" (separate pages) |
| Absolute maximum ratings | "Absolute Maximum Ratings*" (with footnote) |
| Operating conditions | Embedded in DC characteristics tables |
| Electrical characteristics | "DC Characteristics" / "AC Characteristics" |
| Application circuit | "Typical Application Circuit" or "Demo Board Schematic" |

### Espressif

| Content | Typical Section |
|---------|----------------|
| Pin table | Pin description tables in early sections (often §2 or §3) |
| Absolute maximum ratings | "Absolute Maximum Ratings" — often a brief table |
| Operating conditions | "Recommended Operating Conditions" |
| Electrical characteristics | Peripheral-specific tables scattered through the document |
| Application circuit | "Typical Application Schematic" or hardware design guidelines doc |

Note: Espressif often separates the datasheet (pin specs, electrical) from a hardware design guidelines document (application circuit, decoupling). If the PDF is the datasheet only, the application circuit section may be minimal.

### Analog Devices / Maxim

| Content | Typical Section |
|---------|----------------|
| Pin table | "PIN CONFIGURATION" + "PIN DESCRIPTION" (often on page 2) |
| Absolute maximum ratings | "ABSOLUTE MAXIMUM RATINGS" (all caps, early in doc) |
| Operating conditions | Part of main specifications table |
| Electrical characteristics | "ELECTRICAL CHARACTERISTICS" or "DC Specifications" |
| Application circuit | "Typical Application Circuit" or "Applications Information" |

---

## Field-by-Field Guidance

### <code>mpn</code>

Use the exact part number from the datasheet's title or ordering information page, including the package and temperature suffix (e.g., <code>TPS61023DRLR</code>, not <code>TPS61023</code>). Omit marketing names or family names.

### <code>manufacturer</code>

Use the company name as it appears on the datasheet header. For acquisitions (e.g., Maxim by Analog Devices, Linear Technology by Analog Devices), use the name on the datasheet being read, not the current parent company.

### <code>category</code>

Match to the category list in extraction-schema.md. The category determines which scoring rules apply. If the part spans categories (e.g., a PMIC with multiple regulators), use the primary function.

### <code>package</code>

Format: <code>"<package_name> (<pin_count>-pin)"</code>. Examples: <code>"TSSOP-14 (14-pin)"</code>, <code>"SOT-23-6 (6-pin)"</code>, <code>"QFN-32 (32-pin, 5x5mm)"</code>. The pin count is used to validate coverage in the scorer. Found in the ordering information or package outline section.

### <code>pins[].number</code>

Copy exactly as printed in the pin description table: <code>"1"</code>, <code>"2"</code>, <code>"A1"</code>, <code>"EP"</code> (exposed pad). Do not renumber or convert to integers.

### <code>pins[].name</code>

Copy from the pin name column. When the datasheet shows alternative names (e.g., <code>SW/VOUT</code>), use the primary name for the operating mode you're documenting.

### <code>pins[].type</code>

Map from the datasheet's function column:

| Datasheet language | Schema type |
|-------------------|-------------|
| VDD, VCC, VIN, VSUPPLY | <code>power</code> |
| GND, AGND, PGND, VSS | <code>ground</code> |
| FB, COMP, VREF, VSET (analog) | <code>analog</code> |
| EN, SCL, SDA, TX, RX, CS, INT, ALERT | <code>digital</code> |
| NC, No Connect | <code>no_connect</code> |
| SDA/SCL (bidirectional bus), IO | <code>bidirectional</code> |

### <code>pins[].voltage_abs_max</code>

Found in the pin description table (individual pin limits) or the absolute maximum ratings table. Individual pin limits take precedence over the global abs max. Common traps:

- **SW pin on switching regulators**: often has a lower abs max than VIN (e.g., VIN = 6V, SW = 6V, but separate footnote limits SW to 5.6V during startup)
- **ESD clamp pins**: may have a negative lower limit (e.g., <code>-0.3V to 6V</code>) — store only the upper limit in this field
- **I/O pins on MCUs**: often have a separate <code>VDDIO</code> limit distinct from the main supply

### <code>pins[].threshold_high_v</code> / <code>pins[].threshold_low_v</code>

Look in the electrical characteristics table under "Logic Input Threshold" or similar. Common naming:

- <code>V_IH</code>, <code>VIH</code>, <code>V_IL</code>, <code>VIL</code> — standard logic threshold names
- <code>V_EN(H)</code>, <code>V_EN(L)</code> — enable pin thresholds (device-specific naming)
- <code>V_IN(H)</code>, <code>V_IN(L)</code> — input pin thresholds

Trap: Do not confuse <code>V_IH</code> (recommended minimum high input) with <code>V_OL</code>/<code>V_OH</code> (output levels). The extraction wants input thresholds — what the pin recognizes as logic high or low.

Trap: Some datasheets list thresholds as fractions of VDD (e.g., "0.7 × VDD"). Record the fraction notation in the description, and store the absolute value calculated at the nominal operating voltage in the field.

### <code>pins[].required_external</code>

This is the most important field for design review automation. Sources:

1. Pin description "External components required" columns
2. Application circuit notes referencing specific pins
3. Recommended operating conditions footnotes (e.g., "Bypass VIN to GND with 100nF")
4. Absolute maximum ratings notes (e.g., "Place clamp diode on boot pin for inductive loads")

Write in the datasheet's own language when possible. Include values and placement constraints where specified.

---

### <code>absolute_maximum_ratings</code>

Found in the "Absolute Maximum Ratings" table, usually near the front of the datasheet. Key naming conventions:

| Datasheet label | Suggested key | Unit |
|----------------|---------------|------|
| VIN(max), Input Voltage | <code>vin_max_v</code> | V |
| VOUT(max), Output Voltage | <code>vout_max_v</code> | V |
| TJ(max), Junction Temperature | <code>junction_temp_max_c</code> | °C |
| TSTG, Storage Temperature | <code>storage_temp_min_c</code>, <code>storage_temp_max_c</code> | °C |
| ESD (HBM) | <code>esd_hbm_v</code> | V |
| ESD (CDM) | <code>esd_cdm_v</code> | V |

Trap: **Absolute maximum ratings are not operating conditions**. Exposing a pin to its abs max continuously will shorten device lifetime. Do not use abs max values as operating targets.

Trap: Some datasheets list separate abs max for each pin in the pin description table. Capture those in <code>pins[].voltage_abs_max</code>, not here. The top-level absolute_maximum_ratings covers supply voltage and temperature.

---

### <code>recommended_operating_conditions</code>

Found in the "Recommended Operating Conditions" or "Operating Conditions" table. This is the range where the device is guaranteed to perform per the electrical characteristics.

Key traps:

- **VIN vs VOUT vs VDD**: multi-rail devices may have separate operating ranges for each supply. Use the most restrictive or the primary input.
- **Temperature grades**: datasheets may show industrial (−40 to +85°C) and commercial (0 to +70°C) variants in the same table. Extract for the grade you are using.
- **"Conditions" column**: some electrical characteristics tables embed operating conditions as conditions on specific rows. These are test conditions for that row, not the device operating range.

---

### <code>electrical_characteristics.*</code>

Found in the "Electrical Characteristics" table. This table is usually organized with columns: Parameter, Min, Typ, Max, Unit, Test Conditions.

Which value to record:

- Use **Typ** for <code>vref_v</code>, <code>switching_frequency_khz</code> (nominal specs)
- Use **Max** for threshold voltages, quiescent current limits, propagation delay
- Use **Min** for <code>dropout_mv</code> (worst case is what matters for design margin)
- Use **Typ** for SPICE model parameters (behavioral accuracy)

Key field lookup:

| Field | What to find in the table |
|-------|--------------------------|
| <code>vref_v</code> | Reference voltage or feedback threshold; labeled "VREF", "VFB", "Feedback Voltage" |
| <code>switching_frequency_khz</code> | "Oscillator frequency", "Switching frequency", "fSW" |
| <code>quiescent_current_ua</code> | "IQ", "IDD (quiescent)", "Supply current (no load)" — exclude gate drive and switching losses |
| <code>dropout_mv</code> | "Dropout voltage" at rated output current; labeled "VDO", "VIN-VOUT" |
| <code>gbw_hz</code> | "Gain Bandwidth Product", "GBW", "Unity gain frequency" |
| <code>slew_vus</code> | "Slew rate", "SR" — use the slower of rising/falling |
| <code>prop_delay_ns</code> | "Propagation delay", "tPD" — use the worst case across all measurement conditions |
| <code>clamping_voltage_v</code> | "VC", "Clamping voltage" at the specified IEC 61000-4-2 test current |

Trap: **Gain Bandwidth vs Unity Gain Bandwidth**. Some datasheets list both. Use the unity-gain stable bandwidth for <code>gbw_hz</code>.

Trap: **Quiescent vs active supply current**. Record the quiescent (no-load, static) current, not the active switching current, unless the datasheet doesn't distinguish.

---

### <code>application_circuit.*</code>

Found in the typical application circuit section. This section often spans multiple pages with a reference schematic, a component table, and layout notes.

**topology**: Look for how the circuit is described in the section title or opening sentence: "boost converter", "buck converter", "buck-boost", "flyback", "SEPIC", "inverting buck-boost", "LDO". Use lowercase.

**inductor_recommended**: Usually in a "Inductor Selection" subsection or a component table. Capture value, saturation current requirement, and DCR guidance if given.

**input_cap_recommended / output_cap_recommended**: Labeled in the component table or application notes. Common format: <code>"10µF, X5R or X7R, 10V rating"</code>. Include dielectric and voltage rating if specified.

**vout_formula**: Look for a formula in the "Setting the Output Voltage" or "Programming Output Voltage" section. Record the exact formula; do not simplify.

**notes**: Capture layout-critical guidance: copper pour requirements, trace width recommendations, component placement distance constraints, feedback routing warnings.

Trap: Some datasheets have minimal application sections and instead reference an application note (SLVA XXXX, AN1234, etc.). If the datasheet itself has no circuit recommendations, set <code>application_circuit</code> to null and note the reference in <code>extraction_metadata</code>.

---

### <code>spice_specs.*</code>

SPICE specs come from the same electrical characteristics table as <code>electrical_characteristics</code>. The difference is that <code>spice_specs</code> uses the exact key names from <code>spice_part_library.py</code> for direct model generation.

For opamps: <code>gbw_hz</code>, <code>slew_vus</code>, <code>vos_mv</code>, <code>aol_db</code>, <code>rin_ohms</code> come from the electrical characteristics table. <code>supply_min</code>/<code>supply_max</code> come from recommended operating conditions. <code>rro</code>/<code>rri</code> come from the features list or the output swing specification (if the output swings within a few mV of the rail, <code>rro = true</code>).

For regulators: <code>vref</code> comes from the reference voltage spec. <code>dropout_mv</code> from the dropout table. <code>iq_ua</code> from quiescent current. <code>iout_max_ma</code> from the maximum output current rating.

---

## Common Mistakes

**Mixing abs max with operating conditions.** The schematic verifier compares pin voltages against <code>voltage_abs_max</code>; an incorrectly low value will generate false positives.

**Using the wrong table row.** Electrical characteristics tables often have multiple rows for the same parameter under different test conditions (temperature range, load current, VIN). Pick the row matching typical operation, not the extreme test condition.

**Leaving required fields null when the datasheet has them.** If the pin table has a voltage limit column, populate <code>voltage_abs_max</code> for every pin — not just the ones that look interesting. The scorer deducts points for name-only pins.

**Truncating the MPN.** <code>TPS61023</code> and <code>TPS61023DRLR</code> may have different specs (package parasitics, temperature grade). Extract from the correct datasheet and record the full part number.

**Copying from a different variant's table.** Datasheets often cover a family. Check that the table row applies to the specific part number you are extracting.
