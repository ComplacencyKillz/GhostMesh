# Quality Scoring Reference

The scorer (<code>datasheet_score.py</code>) evaluates a completed extraction against a five-dimension completeness rubric and returns a total score from 0.0 to 10.0. The score determines whether the extraction is good enough to cache and use for design review.

---

## Thresholds

| Constant | Value | Source |
|----------|-------|--------|
| <code>MIN_SCORE</code> | 6.0 | <code>datasheet_extract_cache.py</code> |
| <code>MAX_RETRIES</code> | 3 | <code>datasheet_extract_cache.py</code> |
| <code>DEFAULT_MAX_AGE_DAYS</code> | 90 | <code>datasheet_extract_cache.py</code> |

An extraction with <code>total >= 6.0</code> is considered sufficient. An extraction with <code>total < 6.0</code> is retried if <code>retry_count < MAX_RETRIES</code>. Keep the highest-scoring extraction across retries (the cache manager overwrites with each new attempt; stop when sufficient).

---

## Five-Dimension Rubric

| Dimension | Weight | Score Function |
|-----------|--------|---------------|
| Pin coverage | 35% | <code>_score_pin_coverage()</code> |
| Voltage ratings | 25% | <code>_score_voltage_ratings()</code> |
| Application info | 20% | <code>_score_application_info()</code> |
| Electrical characteristics | 10% | <code>_score_electrical_chars()</code> |
| SPICE specs | 10% | <code>_score_spice_specs()</code> |

**Total** = sum of (dimension score × weight), rounded to one decimal place. Weights sum to 1.0.

---

## Dimension Scoring Rules

### Pin Coverage (35%)

Starting score: 10.0.

| Condition | Deduction |
|-----------|-----------|
| No pins in extraction | Score = 0.0 immediately |
| Fewer than 50% of expected pins present | Score = 0.0 immediately |
| Each missing pin (vs expected count) | -2.0 |
| Each pin with name only (no specs, no description, no <code>required_external</code>) | -1.0 |

A pin "has specs" if any of these fields is non-null: <code>voltage_abs_max</code>, <code>voltage_operating_min</code>, <code>voltage_operating_max</code>, <code>current_max_ma</code>, <code>threshold_high_v</code>, <code>threshold_low_v</code>. A pin with only <code>description</code> or <code>required_external</code> (no numeric specs) still avoids the name-only deduction.

If no <code>expected_pin_count</code> is passed to <code>score_extraction()</code>, the expected count defaults to the number of pins in the extraction (no missing-pin deduction).

Issues list is capped at 5 entries for readability.

### Voltage Ratings (25%)

Starting score: 10.0.

| Condition | Deduction |
|-----------|-----------|
| <code>absolute_maximum_ratings</code> dict missing or empty | -3.0 |
| No key ending in <code>_max_v</code> with a non-null value | -2.0 |
| <code>junction_temp_max_c</code> missing | -1.0 |
| <code>recommended_operating_conditions</code> dict missing or empty | -3.0 |
| <code>vin_min_v</code> or <code>vin_max_v</code> missing | -1.5 |
| <code>temp_min_c</code> or <code>temp_max_c</code> missing | -1.0 |

### Application Info (20%)

Starting score: 10.0. If <code>application_circuit</code> dict is missing or empty: score = 0.0 immediately.

| Condition | Deduction |
|-----------|-----------|
| <code>topology</code> missing | -2.0 |
| Zero component recommendation fields populated | -3.0 |
| Only 1 recommendation field populated (expect 2+) | -1.5 |
| Neither <code>vout_formula</code> nor <code>notes</code> populated | -2.0 |
| <code>notes</code> missing (but <code>vout_formula</code> present) | -1.0 |

Recommendation fields counted: <code>inductor_recommended</code>, <code>input_cap_recommended</code>, <code>output_cap_recommended</code>, <code>feedback_resistor_top_ohm</code>, <code>feedback_resistor_bottom_ohm</code>, <code>compensation_cap</code>, <code>bootstrap_cap</code>, <code>decoupling_cap</code>, and any other key ending in <code>_recommended</code>.

### Electrical Characteristics (10%)

Category-dependent. Starting score: 10.0.

If <code>electrical_characteristics</code> dict is missing:
- Categories with required fields → score = 0.0
- Categories without required fields → score = 5.0

| Condition | Deduction |
|-----------|-----------|
| Each missing required field for category | -3.0 |
| Each missing optional (nice-to-have) field | -1.0 |

Required and optional fields by category:

| Category | Required | Optional |
|----------|----------|---------|
| <code>operational_amplifier</code> | <code>gbw_hz</code>, <code>slew_vus</code> | <code>vos_mv</code>, <code>aol_db</code>, <code>rin_ohms</code> |
| <code>comparator</code> | <code>prop_delay_ns</code> | <code>vos_mv</code>, <code>aol_db</code> |
| <code>linear_regulator</code> | <code>vref_v</code>, <code>quiescent_current_ua</code> | <code>dropout_mv</code>, <code>output_current_max_ma</code> |
| <code>switching_regulator</code> | <code>vref_v</code>, <code>switching_frequency_khz</code> | <code>quiescent_current_ua</code>, <code>output_current_max_ma</code> |
| <code>voltage_reference</code> | <code>vref_v</code>, <code>vref_accuracy_pct</code> | <code>temp_coefficient_ppmk</code> |
| <code>microcontroller</code> | (none) | <code>quiescent_current_ua</code>, <code>io_voltage_max</code> |
| <code>esd_protection</code> | <code>clamping_voltage_v</code> | <code>leakage_current_na</code>, <code>capacitance_pf</code> |
| all others | (none) | (none) |

For categories with no required and no optional fields: if 3+ fields are populated, score = 10.0; if 1–2 fields, score = 7.0; if 0 fields, score = 5.0.

Issues list is capped at 5 entries.

### SPICE Specs (10%)

Starting score: 10.0.

| Condition | Score / Deduction |
|-----------|-------------------|
| <code>spice_specs</code> dict missing, but <code>electrical_characteristics</code> present | Score = 5.0 |
| <code>spice_specs</code> dict missing, no electrical chars either | Score = 0.0 |
| <code>spice_specs</code> section is empty (all values null) | Score = 0.0 |
| Each missing required SPICE field for category | -4.0 |
| 1–2 fields populated, no required fields for category | Score capped at 6.0 |

Required SPICE fields by category:

| Category | Required SPICE fields |
|----------|----------------------|
| <code>operational_amplifier</code> | <code>gbw_hz</code> |
| <code>linear_regulator</code> | <code>vref</code>, <code>dropout_mv</code> |
| <code>switching_regulator</code> | <code>vref</code> |
| <code>voltage_reference</code> | <code>vref</code> |
| <code>comparator</code> | (none) |
| all others | (none) |

---

## Score Interpretation

| Score | Meaning |
|-------|---------|
| >= 8.0 | All critical fields present; high confidence for automated pin audit |
| 6.0 – 7.9 | Sufficient for design review; some optional specs missing |
| < 6.0 | Insufficient; cache manager will retry up to <code>MAX_RETRIES</code> times |

An extraction at 5.9 from a part with a minimal datasheet (no application circuit section, no SPICE specs) is not the same as 5.9 from a part whose datasheet has everything but was incompletely extracted. Check the <code>issues</code> list to distinguish.

---

## Calling the Scorer

```python
from datasheet_score import score_extraction

result = score_extraction(extraction, expected_pin_count=6)
```

Returns:

```python
{
    "total": 8.2,
    "pin_coverage": 9.0,
    "voltage_ratings": 8.5,
    "application_info": 7.0,
    "electrical_characteristics": 8.0,
    "spice_specs": 8.5,
    "issues": ["Pin 3 (BOOT): name only, no specs", ...],
    "sufficient": True   # True if total >= 6.0
}
```

The <code>issues</code> list is the union of per-dimension issue strings (capped per dimension). Use it to guide retry attempts — if the issues show "No application circuit information", re-read the application section pages.

---

## Cache Index Entry

When an extraction is cached via <code>cache_extraction()</code>, the <code>manifest.json</code> records a summary:

```json
{
  "file": "TPS61023DRLR_a1b2c3.json",
  "mpn": "TPS61023DRLR",
  "category": "switching_regulator",
  "source_pdf": "TPS61023DRLR.pdf",
  "source_pdf_hash": "sha256:...",
  "extraction_date": "2026-04-15T12:00:00+00:00",
  "extraction_score": 8.2,
  "extraction_version": 1,
  "pin_count": 6
}
```

The <code>extraction_score</code> in the index is the <code>total</code> from <code>score_extraction()</code>, stored in <code>extraction_metadata.extraction_score</code> inside the full JSON. An entry with <code>extraction_score < 6.0</code> may still be present if <code>retry_count >= MAX_RETRIES</code> (retries exhausted).

---

## Staleness Check

<code>is_extraction_stale()</code> in <code>datasheet_extract_cache.py</code> returns <code>(True, reason)</code> if any of:

1. Extraction not found in cache (<code>"not_cached"</code>)
2. <code>extraction_version < EXTRACTION_VERSION</code> (<code>"schema_upgrade"</code>)
3. Source PDF hash changed (<code>"pdf_changed"</code>)
4. Score below <code>MIN_SCORE</code> and retries not exhausted (<code>"low_score (X.X < 6.0, retry N/3)"</code>)
5. Extraction older than 90 days (<code>"age (N days > 90)"</code>)

A stale extraction can still be returned by <code>get_extraction_for_review()</code> along with the stale status; the caller decides whether to use the old data or wait for a refresh.
