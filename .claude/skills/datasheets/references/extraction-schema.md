# Extraction Schema Reference

Canonical schema for structured datasheet extraction JSON files stored in <code>datasheets/extracted/</code>. The extraction itself is performed by the LLM reading selected PDF pages; this document defines what fields the LLM must produce and how the cache manager and verifier interpret them.

Current <code>EXTRACTION_VERSION</code>: **2** (in <code>datasheet_extract_cache.py</code>). Bump this constant when the schema changes to trigger re-extraction of all cached files.

---

## Top-Level Structure

```json
{
  "mpn": "TPS61023DRLR",
  "manufacturer": "Texas Instruments",
  "category": "switching_regulator",
  "package": "SOT-23-6 (6-pin)",
  "description": "1A, 5V, 1.2MHz boost converter with 0.5V input",
  "topology": "boost",
  "pins": [...],
  "features": {...},
  "peripherals": {...},
  "absolute_maximum_ratings": {...},
  "recommended_operating_conditions": {...},
  "electrical_characteristics": {...},
  "application_circuit": {...},
  "spice_specs": {...},
  "extraction_metadata": {...}
}
```

### Top-Level Fields

| Field | Type | Nullable | Description | Added in v2 |
|-------|------|----------|-------------|-------------|
| <code>mpn</code> | string | no | Manufacturer part number, exact including suffix | — |
| <code>manufacturer</code> | string | no | Manufacturer name | — |
| <code>category</code> | string | no | Component category (see category list below) | — |
| <code>package</code> | string | yes | Package name and pin count, e.g. <code>"SOT-23-6 (6-pin)"</code> | — |
| <code>description</code> | string | yes | One-line description from datasheet | — |
| <code>topology</code> | string | yes | Circuit topology: <code>'boost'</code>, <code>'buck'</code>, <code>'ldo'</code>, <code>'mcu'</code>, <code>'sensor'</code>, <code>'adc'</code>, <code>'mosfet'</code>, <code>'bjt'</code>, <code>'other'</code> | yes |
| <code>pins</code> | array | no | Per-pin specifications (may be empty if pin table not found) | — |
| <code>features</code> | object | yes | Device-specific feature flags (has_pg, has_soft_start, etc.) | yes |
| <code>peripherals</code> | object | yes | Peripheral specifications for MCUs (usb, etc.) | yes |
| <code>absolute_maximum_ratings</code> | object | yes | Absolute limits; null if section not found | — |
| <code>recommended_operating_conditions</code> | object | yes | Operating ranges; null if section not found | — |
| <code>electrical_characteristics</code> | object | yes | Category-dependent key specs | — |
| <code>application_circuit</code> | object | yes | Reference design and component recommendations | — |
| <code>spice_specs</code> | object | yes | SPICE behavioral model parameters | — |
| <code>extraction_metadata</code> | object | no | Cache bookkeeping (source PDF, score, version) | — |

---

## Category Values

Use these exact strings — they match <code>_classify_ic_function()</code> in <code>analyze_schematic.py</code>.

| Category | Typical parts |
|----------|--------------|
| <code>microcontroller</code> | STM32, ESP32, ATmega, RP2040 |
| <code>operational_amplifier</code> | LM358, OPA340, AD8605 |
| <code>comparator</code> | LM393, TLV3501 |
| <code>linear_regulator</code> | AMS1117, LM1117, AP2112 |
| <code>switching_regulator</code> | TPS61023, LM2596, MP2307 |
| <code>voltage_reference</code> | REF3030, LM4040 |
| <code>esd_protection</code> | USBLC6-2SC6, PRTR5V0U2X |
| <code>adc</code> | ADS1115, MCP3008 |
| <code>dac</code> | MCP4725, DAC8552 |
| <code>interface</code> | MAX232, SN65HVD230, CP2102 |
| <code>memory</code> | AT24C256, W25Q128 |
| <code>sensor</code> | BME280, MPU6050 |
| <code>led_driver</code> | TLC5940, WS2812B |
| <code>motor_driver</code> | DRV8833, A4988 |
| <code>power_management</code> | BQ24074, TPS2113 |
| <code>fpga</code> | ICE40, XC7A |
| <code>rf</code> | CC1101, SX1276 |
| <code>audio</code> | MAX98357, PCM5102 |

---

## <code>pins[]</code>

Array of pin entries. The schematic verifier joins on <code>pin.number</code> (string) to the schematic's <code>pin_nets</code> map.

### Pin Entry Fields

| Field | Type | Unit | Nullable | Example | Description | Added in v2 |
|-------|------|------|----------|---------|-------------|-------------|
| <code>number</code> | string | — | no | <code>"1"</code>, <code>"A3"</code>, <code>"EP"</code> | Pin number as shown on datasheet | — |
| <code>name</code> | string | — | no | <code>"SW"</code> | Pin name from datasheet | — |
| <code>function</code> | string | — | yes | <code>"EN"</code> | Functional category; see values below | yes |
| <code>type</code> | string | — | no | <code>"power"</code> | Functional type (see values below) | — |
| <code>direction</code> | string | — | yes | <code>"bidirectional"</code> | Signal direction (see values below) | — |
| <code>description</code> | string | — | yes | <code>"Inductor switch node"</code> | Brief functional description | — |
| <code>voltage_abs_max</code> | float | V | yes | <code>6.0</code> | Absolute maximum voltage on this pin | — |
| <code>voltage_operating_min</code> | float | V | yes | <code>0.5</code> | Minimum recommended operating voltage | — |
| <code>voltage_operating_max</code> | float | V | yes | <code>5.5</code> | Maximum recommended operating voltage | — |
| <code>current_max_ma</code> | float | mA | yes | <code>3600</code> | Maximum current through this pin | — |
| <code>internal_connection</code> | string | — | yes | <code>"Power FET drain"</code> | What this pin connects to internally | — |
| <code>required_external</code> | string | — | yes | <code>"0.47-2.2uH inductor"</code> | What must be connected — primary field for pin audit | — |
| <code>threshold_high_v</code> | float | V | yes | <code>1.2</code> | Logic high threshold (digital input pins) | — |
| <code>threshold_low_v</code> | float | V | yes | <code>0.4</code> | Logic low threshold (digital input pins) | — |
| <code>has_internal_pullup</code> | bool | — | yes | <code>true</code> | Pin has internal pull-up resistor | — |
| <code>has_internal_pulldown</code> | bool | — | yes | <code>false</code> | Pin has internal pull-down resistor | — |

### <code>function</code> Values (v2)

Canonical pin functional categories, used by <code>get_pin_function()</code> in <code>datasheet_features.py</code>.

| Value | Description |
|-------|-------------|
| <code>'VIN'</code> | Input power supply pin |
| <code>'VOUT'</code> | Output voltage or regulated output |
| <code>'EN'</code> | Enable control input |
| <code>'PG'</code> | Power-good indicator output |
| <code>'SW'</code> | Switching node (regulators) |
| <code>'FB'</code> | Feedback input (regulators) |
| <code>'GND'</code> | Ground pin |
| <code>'IO'</code> | General-purpose I/O |
| <code>'CLK'</code> | Clock signal |
| <code>'RESET'</code> | Reset control |
| <code>'OTHER'</code> | Other function not in above list |
| <code>None</code> | Function not specified or unknown |

### <code>type</code> Values

| Value | Description |
|-------|-------------|
| <code>power</code> | Supply voltage input or output |
| <code>ground</code> | Ground connection |
| <code>analog</code> | Analog signal (feedback, sense, reference) |
| <code>digital</code> | Digital signal (logic I/O, enable, clock) |
| <code>no_connect</code> | NC pin — must not be connected |
| <code>bidirectional</code> | Can be input or output depending on configuration |

### <code>direction</code> Values

| Value | Description |
|-------|-------------|
| <code>input</code> | Signal flows into the device |
| <code>output</code> | Signal driven by the device |
| <code>bidirectional</code> | Both input and output |
| <code>passive</code> | No inherent direction (power, ground) |

### <code>required_external</code> Examples

This field is the primary driver for the missing-external-component check. Use the datasheet's own language where possible.

- <code>"Connect to inductor (0.47-2.2uH recommended)"</code>
- <code>"10K pull-up to VCC required"</code>
- <code>"Bypass cap 100nF to GND, place within 3mm"</code>
- <code>"Resistor divider from VOUT, Vout = 0.595 * (1 + R1/R2)"</code>
- <code>"Do not connect"</code> (NC pins)
- <code>"Connect to VIN for always-on, or logic control. Do not float."</code>

---

## <code>features</code> (v2)

Device-specific feature flags. This object is null if not applicable to the device category.

| Key | Type | Nullable | Description |
|-----|------|----------|-------------|
| <code>has_pg</code> | bool | yes | Part has a power-good output pin |
| <code>has_soft_start</code> | bool | yes | Device has integrated soft-start circuit |
| <code>iss_time_us</code> | float | yes | Soft-start time constant in microseconds |

---

## <code>peripherals</code> (v2)

Peripheral specifications for MCUs and similar devices. This object is null if not applicable.

### <code>peripherals.usb</code>

USB interface specifications. Null if device does not have USB.

| Key | Type | Nullable | Description |
|-----|------|----------|-------------|
| <code>speed</code> | string | yes | USB speed: <code>'FS'</code> (full-speed), <code>'HS'</code> (high-speed), <code>'SS'</code> (super-speed), or <code>None</code> |
| <code>native_phy</code> | bool | yes | Device has native USB PHY (vs external PHY required) |
| <code>series_r_required</code> | bool | yes | Series termination resistors required on D+/D- |

---

## <code>absolute_maximum_ratings</code>

Use keys with the suffix <code>_max_v</code>, <code>_max_c</code>, <code>_max_ma</code>, or <code>_v</code> as appropriate for the physical quantity. Null means the datasheet did not specify the limit.

| Key | Type | Unit | Example | Description |
|-----|------|------|---------|-------------|
| <code>vin_max_v</code> | float | V | <code>6.0</code> | Input voltage absolute maximum |
| <code>vout_max_v</code> | float | V | <code>6.0</code> | Output voltage absolute maximum |
| <code>io_voltage_max</code> | float | V | <code>4.0</code> | I/O pin voltage maximum (MCUs) |
| <code>junction_temp_max_c</code> | float | °C | <code>150</code> | Maximum junction temperature |
| <code>storage_temp_min_c</code> | float | °C | <code>-65</code> | Minimum storage temperature |
| <code>storage_temp_max_c</code> | float | °C | <code>150</code> | Maximum storage temperature |
| <code>esd_hbm_v</code> | float | V | <code>2000</code> | ESD rating, Human Body Model |
| <code>esd_cdm_v</code> | float | V | <code>500</code> | ESD rating, Charged Device Model |

Add device-specific keys as needed (e.g., <code>sw_pin_max_v</code>, <code>boot_voltage_max_v</code>). The scoring check looks for any key ending in <code>_max_v</code> to confirm voltage abs max is present.

---

## <code>recommended_operating_conditions</code>

| Key | Type | Unit | Example | Description |
|-----|------|------|---------|-------------|
| <code>vin_min_v</code> | float | V | <code>0.5</code> | Minimum input voltage |
| <code>vin_max_v</code> | float | V | <code>5.5</code> | Maximum input voltage |
| <code>vout_min_v</code> | float | V | <code>1.8</code> | Minimum output voltage |
| <code>vout_max_v</code> | float | V | <code>5.5</code> | Maximum output voltage |
| <code>temp_min_c</code> | float | °C | <code>-40</code> | Minimum operating temperature |
| <code>temp_max_c</code> | float | °C | <code>85</code> | Maximum operating temperature |

The scorer checks for <code>vin_min_v</code> + <code>vin_max_v</code> and <code>temp_min_c</code> + <code>temp_max_c</code>; missing either pair deducts from the voltage_ratings score.

---

## <code>electrical_characteristics</code>

Category-dependent. The scorer checks for category-specific required and optional fields.

### Switching regulators

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>vref_v</code> | float | V | required | Feedback reference voltage |
| <code>switching_frequency_khz</code> | float | kHz | required | Nominal switching frequency |
| <code>quiescent_current_ua</code> | float | µA | optional | Quiescent supply current |
| <code>output_current_max_ma</code> | float | mA | optional | Maximum output current |
| <code>vref_accuracy_pct</code> | float | % | optional | Reference voltage accuracy |
| <code>efficiency_pct</code> | float | % | optional | Peak efficiency |
| <code>shutdown_current_ua</code> | float | µA | optional | Shutdown/sleep supply current |

### Linear regulators

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>vref_v</code> | float | V | required | Reference / output voltage |
| <code>quiescent_current_ua</code> | float | µA | required | Quiescent supply current |
| <code>dropout_mv</code> | float | mV | optional | Dropout voltage at rated current |
| <code>output_current_max_ma</code> | float | mA | optional | Maximum output current |

### Operational amplifiers

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>gbw_hz</code> | float | Hz | required | Gain-bandwidth product |
| <code>slew_vus</code> | float | V/µs | required | Slew rate |
| <code>vos_mv</code> | float | mV | optional | Input offset voltage |
| <code>aol_db</code> | float | dB | optional | Open-loop gain |
| <code>rin_ohms</code> | float | Ω | optional | Input impedance |
| <code>cmrr_db</code> | float | dB | optional | Common-mode rejection ratio |

### Comparators

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>prop_delay_ns</code> | float | ns | required | Propagation delay |
| <code>vos_mv</code> | float | mV | optional | Input offset voltage |
| <code>aol_db</code> | float | dB | optional | Open-loop gain |

### Voltage references

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>vref_v</code> | float | V | required | Reference output voltage |
| <code>vref_accuracy_pct</code> | float | % | required | Initial accuracy |
| <code>temp_coefficient_ppmk</code> | float | ppm/°C | optional | Temperature coefficient |

### ESD protection

| Key | Type | Unit | Scoring | Description |
|-----|------|------|---------|-------------|
| <code>clamping_voltage_v</code> | float | V | required | Clamping voltage at rated surge current |
| <code>leakage_current_na</code> | float | nA | optional | Reverse leakage current |
| <code>capacitance_pf</code> | float | pF | optional | Line capacitance |

### Microcontrollers

No universally required fields (MCU characteristics vary too widely). Optional fields include <code>quiescent_current_ua</code> and <code>io_voltage_max</code>. Include whatever the datasheet provides.

---

## <code>application_circuit</code>

| Key | Type | Nullable | Example | Description |
|-----|------|----------|---------|-------------|
| <code>topology</code> | string | yes | <code>"boost"</code> | Circuit topology |
| <code>inductor_recommended</code> | string | yes | <code>"1uH, Isat > 3.6A"</code> | Recommended inductor |
| <code>input_cap_recommended</code> | string | yes | <code>"10uF ceramic, X5R or X7R"</code> | Input capacitor recommendation |
| <code>output_cap_recommended</code> | string | yes | <code>"22uF ceramic x2"</code> | Output capacitor recommendation |
| <code>feedback_resistor_top_ohm</code> | float | yes | <code>1000000</code> | Top feedback resistor value (Ω) |
| <code>feedback_resistor_bottom_ohm</code> | float | yes | <code>845000</code> | Bottom feedback resistor value (Ω) |
| <code>compensation_cap</code> | string | yes | <code>"22pF"</code> | Compensation capacitor |
| <code>bootstrap_cap</code> | string | yes | <code>"100nF"</code> | Bootstrap capacitor |
| <code>decoupling_cap</code> | string | yes | <code>"100nF per VDD pin"</code> | Decoupling recommendation |
| <code>vout_formula</code> | string | yes | <code>"Vout = 0.595 * (1 + R1/R2)"</code> | Output voltage formula |
| <code>notes</code> | array | yes | <code>["Place input cap close to IC", ...]</code> | Layout and application notes |

The scoring check counts fields matching <code>_recommended</code> or in the set <code>{inductor_recommended, input_cap_recommended, output_cap_recommended, feedback_resistor_top_ohm, feedback_resistor_bottom_ohm, compensation_cap, bootstrap_cap, decoupling_cap}</code>. Two or more populated fields scores full marks; zero scores 0.

Additional <code>_recommended</code>-suffixed keys are also counted. Add device-specific keys freely.

---

## <code>spice_specs</code>

Uses the same key names as <code>spice_part_library.py</code> to allow direct consumption by the SPICE model generator without field mapping.

| Key | Type | Unit | Nullable | Description |
|-----|------|------|----------|-------------|
| <code>gbw_hz</code> | float | Hz | yes | Gain-bandwidth product (opamps) |
| <code>slew_vus</code> | float | V/µs | yes | Slew rate (opamps) |
| <code>vos_mv</code> | float | mV | yes | Input offset voltage (opamps) |
| <code>aol_db</code> | float | dB | yes | Open-loop gain (opamps) |
| <code>rin_ohms</code> | float | Ω | yes | Input impedance (opamps) |
| <code>supply_min</code> | float | V | yes | Minimum supply voltage |
| <code>supply_max</code> | float | V | yes | Maximum supply voltage |
| <code>rro</code> | bool | — | yes | Rail-to-rail output |
| <code>rri</code> | bool | — | yes | Rail-to-rail input |
| <code>swing_v</code> | float | V | yes | Output swing from rail (non-RRO) |
| <code>dropout_mv</code> | float | mV | yes | Dropout voltage (LDOs) |
| <code>iq_ua</code> | float | µA | yes | Quiescent current |
| <code>iout_max_ma</code> | float | mA | yes | Maximum output current |
| <code>vref</code> | float | V | yes | Reference voltage |

Populate only the fields relevant to the component category. Null means the value was not found in the datasheet — do not guess.

---

## <code>extraction_metadata</code>

The extractor populates <code>source_pdf</code> and <code>extracted_from_pages</code>. The cache manager fills the remaining fields automatically.

| Key | Type | Nullable | Description |
|-----|------|----------|-------------|
| <code>source_pdf</code> | string | yes | Filename of the source PDF (relative to <code>datasheets/</code>) |
| <code>source_pdf_hash</code> | string | yes | <code>"sha256:<hex>"</code> — set by cache manager |
| <code>extracted_from_pages</code> | array | yes | Page numbers read during extraction |
| <code>total_pdf_pages</code> | int | yes | Total pages in the source PDF |
| <code>extraction_date</code> | string | no | ISO 8601 UTC timestamp — set by cache manager |
| <code>extraction_score</code> | float | no | Total score 0.0–10.0 — set after scoring |
| <code>score_breakdown</code> | object | yes | Per-dimension scores (see quality-scoring.md) |
| <code>extraction_version</code> | int | no | Schema version — set by cache manager (<code>EXTRACTION_VERSION</code>) |
| <code>retry_count</code> | int | no | How many extraction attempts have been made |

---

## Manifest File

<code>datasheets/extracted/manifest.json</code> (legacy name: <code>index.json</code>) tracks all cached extractions. The cache manager reads and writes this file; extraction code does not need to update it directly.

```json
{
  "version": 1,
  "last_updated": "2026-04-15T12:00:00+00:00",
  "extractions": {
    "TPS61023DRLR_a1b2c3": {
      "file": "TPS61023DRLR_a1b2c3.json",
      "mpn": "TPS61023DRLR",
      "category": "switching_regulator",
      "source_pdf": "TPS61023DRLR.pdf",
      "source_pdf_hash": "sha256:...",
      "extraction_date": "2026-04-15T12:00:00+00:00",
      "extraction_score": 9.1,
      "extraction_version": 1,
      "pin_count": 6
    }
  }
}
```

Index keys are MPN strings sanitized by <code>_sanitize_mpn()</code>: non-alphanumeric characters replaced with underscores, with a 6-character MD5 suffix appended to avoid collisions (e.g., <code>TPS61023DRLR_a1b2c3</code>). The suffix is derived from the raw MPN.

---

## Schema Versioning

<code>EXTRACTION_VERSION</code> is an integer in <code>datasheet_extract_cache.py</code>. When the schema changes in a backward-incompatible way, bump this constant. Helper functions in <code>datasheet_features.py</code> check the stored <code>extraction_version</code> against the current constant; any extraction with an older version is treated as unavailable.

### Version History

| Version | Date | Changes |
|---------|------|---------|
| 2 | 2026-04-15 | Added <code>topology</code> (top-level), <code>features</code> object (has_pg, has_soft_start, iss_time_us), <code>peripherals.usb</code> object (speed, native_phy, series_r_required), <code>pins[].function</code> (canonical pin category) |
| 1 | (original) | Base schema with pins, electrical_characteristics, application_circuit, spice_specs, etc. |
