# Consumer API Reference

How to consume structured datasheet extractions in analyzer code. Covers the <code>datasheet_features.py</code> helper, the raw cache access functions from <code>datasheet_extract_cache.py</code>, and the skip-with-INFO pattern for detectors that require extraction data.

---

## datasheet_features.py

This module provides typed accessors that abstract cache lookup, null safety, and field path traversal.

### <code>get_regulator_features(mpn, *, extract_dir=None, analysis_json=None, project_dir=None) -> dict | None</code>

Returns a dict of regulator-relevant fields from the extraction, or <code>None</code> if no extraction is available.

Returns <code>None</code> when:
- No extraction is cached for the MPN
- Extraction is stale (below <code>EXTRACTION_VERSION</code>)
- Extraction score is below <code>MIN_SCORE</code> (6.0)
- Extraction topology is not one of: <code>'boost'</code>, <code>'buck'</code>, <code>'ldo'</code>

```python
from datasheet_features import get_regulator_features

feat = get_regulator_features('TPS61023DRLR')
# Returns:
# {
#     'topology': 'boost',
#     'has_pg': False,           # None if unknown
#     'has_soft_start': True,    # None if unknown
#     'iss_time_us': 12.5,       # None if unknown
#     'en_v_ih_max': 0.96,       # None if not in extraction
#     'en_v_il_min': 0.4,        # None if not in extraction
#     'vin_pin': '2',            # Pin number (str) or None
#     'vout_pin': '3',           # Pin number (str) or None
#     'en_pin': '1',             # Pin number (str) or None
#     'pg_pin': None,            # Pin number (str) or None
# }
# or None if no extraction exists for this MPN
```

Returned dict fields:

| Field | Type | Description |
|-------|------|-------------|
| <code>topology</code> | <code>'boost' \| 'buck' \| 'ldo'</code> | Circuit topology |
| <code>has_pg</code> | <code>bool \| None</code> | Part has a power-good output pin |
| <code>has_soft_start</code> | <code>bool \| None</code> | Integrated soft-start circuit |
| <code>iss_time_us</code> | <code>float \| None</code> | Soft-start time in microseconds |
| <code>en_v_ih_max</code> | <code>float \| None</code> | EN pin logic-high threshold (V) |
| <code>en_v_il_min</code> | <code>float \| None</code> | EN pin logic-low threshold (V) |
| <code>vin_pin</code> | <code>str \| None</code> | Pin number of VIN pin |
| <code>vout_pin</code> | <code>str \| None</code> | Pin number of VOUT pin |
| <code>en_pin</code> | <code>str \| None</code> | Pin number of EN pin |
| <code>pg_pin</code> | <code>str \| None</code> | Pin number of PG pin |

### <code>get_mcu_features(mpn, *, extract_dir=None, analysis_json=None, project_dir=None) -> dict | None</code>

Returns MCU-relevant fields, or <code>None</code> if no extraction is available.

Returns <code>None</code> when:
- No extraction is cached for the MPN
- Extraction is stale or below <code>MIN_SCORE</code>
- Extraction topology is not <code>'mcu'</code>

```python
from datasheet_features import get_mcu_features

mcu = get_mcu_features('ESP32-S3')
# Returns:
# {
#     'usb_speed': 'FS',          # 'FS' | 'HS' | 'SS' | None
#     'has_native_usb_phy': True, # None if unknown
#     'usb_series_r_required': True,
# }
# or None if no extraction exists
```

Returned dict fields:

| Field | Type | Description |
|-------|------|-------------|
| <code>usb_speed</code> | <code>'FS' \| 'HS' \| 'SS' \| None</code> | USB device speed |
| <code>has_native_usb_phy</code> | <code>bool \| None</code> | Native USB PHY present |
| <code>usb_series_r_required</code> | <code>bool \| None</code> | Series termination resistors required |

### <code>get_pin_function(mpn, pin_identifier, *, extract_dir=None, analysis_json=None, project_dir=None) -> str | None</code>

Returns the functional category of a pin, or <code>None</code> if not found or extraction unavailable.

<code>pin_identifier</code> matches against <code>pins[].number</code> (exact match, string) or <code>pins[].name</code> (case-insensitive).

```python
from datasheet_features import get_pin_function

fn = get_pin_function('TPS61023DRLR', 'EN')
# Returns: 'EN' (the pin's function field)
# or None if extraction missing or pin not found
```

Possible return values: <code>'VIN'</code>, <code>'VOUT'</code>, <code>'EN'</code>, <code>'PG'</code>, <code>'SW'</code>, <code>'FB'</code>, <code>'GND'</code>, <code>'IO'</code>, <code>'CLK'</code>, <code>'RESET'</code>, <code>'OTHER'</code>, or <code>None</code>.

### <code>is_extraction_available(mpn, *, extract_dir=None, analysis_json=None, project_dir=None) -> bool</code>

Returns <code>True</code> iff a v2+, sufficiently-scored extraction exists for the MPN.

```python
from datasheet_features import is_extraction_available

if is_extraction_available('TPS61023DRLR'):
    # Safe to call get_regulator_features()
    pass
```

---

## None Contract

The contract for all helper functions: individual fields within a returned dict may be <code>None</code>, distinct from <code>False</code> or <code>0</code>.

| Value | Meaning | Detector action |
|-------|---------|----------------|
| <code>None</code> (whole return) | No extraction cached, stale, or low score | Skip the check; emit INFO |
| <code>None</code> (field within dict) | Datasheet didn't specify this field | Treat as unknown; do not fire checks based on this field |
| <code>False</code> | Datasheet explicitly says feature is absent | Fire relevant checks if configured |
| <code>0</code> / <code>0.0</code> | Datasheet specifies zero | Treat as numeric zero |

Detectors must distinguish <code>None</code> (unknown) from <code>False</code> (explicitly no). Example: <code>has_pg=None</code> means unknown; <code>has_pg=False</code> means confirmed absent.

---

## Skip Pattern for Detectors

When a detector needs extraction data and the helper function returns <code>None</code> (whole function return), emit an INFO-level finding and return — do not fire the rule.

```python
feat = get_regulator_features(mpn)
if feat is None:
    findings.append({
        "severity": "INFO",
        "rule_id": "SS-004",
        "summary": (
            f"Check SS-004 skipped for {ref}: no datasheet extraction for {mpn}. "
            f"Run sync_datasheets to download and extract."
        ),
        "detector": "audit_soft_start",
    })
    return findings

# Now safe to use feat['has_soft_start'], etc.
if feat['has_soft_start'] is None:
    # Field not in extraction; skip this particular sub-check
    pass
elif feat['has_soft_start']:
    # Feature present; run the check
    pass
```

Format for the skip message:
```
Check <rule_id> skipped for <ref>: no datasheet extraction for <mpn>. Run sync_datasheets to download and extract.
```

Do not emit a warning or error for a missing extraction — INFO is the correct severity. The extraction is optional; its absence means the check cannot run, not that there is a design problem.

---

## Direct Cache Access

For cases where the helper functions do not cover the needed field, use the cache functions directly.

### <code>get_cached_extraction(extract_dir, mpn) -> dict | None</code>

Returns the full extraction dict, or <code>None</code> if not cached.

```python
from datasheet_extract_cache import get_cached_extraction, resolve_extract_dir

extract_dir = resolve_extract_dir(analysis_json=analysis)
extraction = get_cached_extraction(extract_dir, mpn)
if extraction is None:
    # no cached data — skip checks that need it
    pass
```

### <code>resolve_extract_dir(analysis_json=None, project_dir=None, override_dir=None) -> Path</code>

Resolves the <code>datasheets/extracted/</code> directory for a project:

1. If <code>override_dir</code> is set, use it directly.
2. If <code>project_dir</code> is set, use <code><project_dir>/datasheets/extracted/</code>.
3. If <code>analysis_json</code> is provided, use the <code>"file"</code> field to find the project root.
4. Fallback: system temp directory.

```python
extract_dir = resolve_extract_dir(project_dir="/path/to/project")
```

---

## Resolving extract_dir in Detectors

Detectors called from <code>analyze_schematic.py</code> receive an <code>AnalysisContext</code> object. The extraction directory is resolved once at the top of the analysis run and passed through context.

```python
# In analyze_schematic.py (caller side)
from datasheet_extract_cache import resolve_extract_dir
ctx.extract_dir = resolve_extract_dir(analysis_json=analysis_data)

# In a detector (receiver side)
extract_dir = ctx.extract_dir  # Path or None
if not extract_dir or not extract_dir.exists():
    return []  # skip silently — no extraction infrastructure set up
```

If <code>extract_dir</code> does not exist, skip silently (no INFO finding) — this is the normal state for projects that have not run the extraction pipeline.

---

## Quality Gate

The helper functions (<code>get_regulator_features()</code>, <code>get_mcu_features()</code>, etc.) apply a quality gate transparently:
- Extractions with <code>extraction_metadata.extraction_score < MIN_SCORE</code> (6.0) are treated as unavailable.
- Extractions with <code>extraction_metadata.extraction_version < EXTRACTION_VERSION</code> are stale and treated as unavailable.

This means a <code>None</code> return from a helper does not require the detector to check the score separately. For direct cache access, apply the same gate:

```python
from datasheet_extract_cache import get_cached_extraction, EXTRACTION_VERSION, MIN_SCORE

ext = get_cached_extraction(extract_dir, mpn)
if not ext:
    return []  # skip

meta = ext.get('extraction_metadata') or {}
if (meta.get('extraction_version') or 0) < EXTRACTION_VERSION:
    return []  # stale, skip

if (meta.get('extraction_score') or 0) < MIN_SCORE:
    return []  # low quality, skip
```
