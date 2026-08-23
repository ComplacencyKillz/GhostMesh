# .kicad-happy.json Config Reference

Configuration file for kicad-happy. Placed in the project directory (or any parent directory). All analyzers, the EMC skill, and the BOM skill read this file automatically.

## File Format

JSONC — JSON with <code>//</code> and <code>/* */</code> comments, and trailing commas allowed. The parser is purely stdlib; no external dependencies.

```jsonc
// Comments are allowed anywhere
{
  "version": 1, // trailing commas are OK
  "project": {
    "name": "My Board",
  },
}
```

## Discovery and Merge Order

The loader walks upward from the project directory, collecting every <code>.kicad-happy.json</code> it finds, then includes <code>~/.kicad-happy.json</code> as the base layer (lowest precedence). Files are merged closest-wins:

```
~/.kicad-happy.json          ← base layer (user-wide defaults)
/home/user/hw/.kicad-happy.json   ← workspace layer
/home/user/hw/myboard/.kicad-happy.json  ← project layer (wins)
```

**Merge rules:**
- Dict values: deep-merged recursively; closer keys win on conflict.
- <code>suppressions</code>: **concatenated** across all layers (additive — all suppressions apply).
- All other lists: closer layer wins entirely (replaces the farther layer's list).

**Error handling:** Parse errors print a warning to stderr and skip that layer. The loader never crashes. Invalid field values are warned and skipped individually.

## Schema

### <code>version</code> (integer)

Always <code>1</code>. Reserved for future schema evolution.

---

### <code>project</code> (object)

Document metadata. Consumed by analyzers for report context.

| Field | Type | Description |
|-------|------|-------------|
| <code>name</code> | string | Product or project name |
| <code>number</code> | string | Model or part number |
| <code>revision</code> | string | Document revision (e.g., "A", "1.2") |
| <code>company</code> | string | Manufacturer or organization name |
| <code>author</code> | string | Document author |
| <code>market</code> | string | Compliance market: <code>"us"</code>, <code>"eu"</code>, <code>"automotive"</code>, <code>"medical"</code>, <code>"military"</code> |
| <code>ambient_temperature_c</code> | number | Ambient temperature for thermal analysis (default: <code>25</code>) |
| <code>emc_standard</code> | string | EMC standard: <code>"fcc-class-b"</code>, <code>"fcc-class-a"</code>, <code>"cispr-class-a"</code>, <code>"cispr-class-b"</code> |
| <code>compliance_market</code> | string | Same as <code>market</code>; used by the EMC analyzer |

---

### <code>suppressions</code> (array of objects)

Suppress specific analyzer findings. **Additive across config layers** — suppressions from all discovered config files are combined. Findings are marked suppressed, never removed; suppressed findings still appear in reports with a note.

Each entry:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| <code>rule_id</code> | string | **Yes** | Exact rule ID to suppress (e.g., <code>"DC-001"</code>, <code>"SW-002"</code>) |
| <code>components</code> | array of strings | No | fnmatch glob patterns for component refs. At least one finding component must match at least one pattern. |
| <code>nets</code> | array of strings | No | fnmatch glob patterns for net names. At least one finding net must match at least one pattern. |
| <code>reason</code> | string | No | Human-readable explanation; shown in reports |

**Matching:** <code>rule_id</code> must match exactly. If <code>components</code> is present, the finding must reference at least one matching component. If <code>nets</code> is present, the finding must reference at least one matching net. All present filters must match (AND logic). Patterns use Python <code>fnmatch</code> syntax (<code>*</code>, <code>?</code>, <code>[seq]</code>).

**Entries missing <code>rule_id</code> are silently skipped** with a stderr warning.

```jsonc
"suppressions": [
  // Suppress for all instances of this rule
  {
    "rule_id": "DC-001",
    "reason": "Intentional — test pad left floating"
  },
  // Suppress only for specific components
  {
    "rule_id": "SW-002",
    "components": ["Q1", "Q2"],
    "reason": "Bootstrap topology confirmed with vendor"
  },
  // Suppress only for specific nets
  {
    "rule_id": "EMC-005",
    "nets": ["VBUS_RAW", "USB_*"],
    "reason": "USB VBUS handled by upstream filter board"
  },
]
```

---

### <code>preferred_suppliers</code> (array of strings) — v1.2

Ordered list of preferred suppliers for BOM sourcing. The BOM manager uses this to select the primary distributor instead of auto-detecting. First entry is primary.

Valid values: <code>"digikey"</code>, <code>"mouser"</code>, <code>"lcsc"</code>, <code>"element14"</code>.

Unknown values are warned and dropped. Default: <code>[]</code> (BOM manager auto-selects).

```jsonc
"preferred_suppliers": ["lcsc", "digikey"]
```

---

### <code>bom</code> (object) — v1.2

BOM conventions for this project.

| Field | Type | Description |
|-------|------|-------------|
| <code>field_priority</code> | array of strings | Ordered list of schematic field names to search for part numbers (e.g., <code>["MPN", "Digi-Key_PN"]</code>). Informational — guides the AI agent; no code enforcement. |
| <code>group_by</code> | string | How to group BOM lines: <code>"value"</code>, <code>"mpn"</code>, or <code>"value+footprint"</code> (default: <code>"value+footprint"</code>) |

Invalid <code>group_by</code> values are warned and the field is ignored (default behavior applies).

```jsonc
"bom": {
  "field_priority": ["MPN", "LCSC", "Digi-Key_PN"],
  "group_by": "mpn"
}
```

---

### <code>analysis</code> (object)

Controls analysis script behavior and output.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| <code>output_dir</code> | string | <code>"analysis"</code> | Directory for analysis JSON output |
| <code>retention</code> | integer | <code>5</code> | Number of past analysis runs to keep |
| <code>auto_diff</code> | boolean | <code>true</code> | Automatically diff against the previous run |
| <code>track_in_git</code> | boolean | <code>false</code> | Include analysis output in git tracking |
| <code>diff_threshold</code> | string | <code>"major"</code> | Minimum change level to report: <code>"major"</code>, <code>"minor"</code>, or <code>"all"</code> |
| <code>power_rails</code> | object | <code>{}</code> | Power rail filtering and annotation (see below) |

#### <code>analysis.power_rails</code> (object) — v1.2

Filter and annotate power rails in analysis output. All patterns use fnmatch glob syntax.

| Field | Type | Description |
|-------|------|-------------|
| <code>ignore</code> | array of strings | Net name patterns to exclude from analysis. Ignored rails are removed from <code>rail_voltages</code>, <code>power_rails</code>, sleep current audit, and power tree figures. |
| <code>flag</code> | array of strings | Net name patterns to highlight for extra scrutiny. Flagged rails appear in top-level <code>flagged_rails</code>. |
| <code>voltage_overrides</code> | object | Manual voltage assignments: <code>{net_name: voltage_float}</code>. Overrides auto-detected voltages from regulator outputs and power symbol name inference. |

<code>ignore</code> and <code>flag</code> must be arrays; <code>voltage_overrides</code> values must be numeric. Invalid entries are warned and skipped.

```jsonc
"analysis": {
  "output_dir": "analysis",
  "retention": 5,
  "auto_diff": true,
  "track_in_git": false,
  "diff_threshold": "major",
  "power_rails": {
    "ignore": ["VBUS_RAW", "USB_*", "BOOT_*"],
    "flag": ["+12V_UNREGULATED", "BATT_*"],
    "voltage_overrides": {
      "+3V3_MCU": 3.3,
      "VDDIO": 1.8
    }
  }
}
```

---

### <code>design_intent</code> (object)

Explicit design intent overrides. When absent, each field is **auto-detected** from PCB fab notes, schematic title blocks, component MPNs, and board characteristics. See <code>design-intent.md</code> for the full auto-detection logic and per-market review priorities.

| Field | Type | Auto-detected when absent | Description |
|-------|------|--------------------------|-------------|
| <code>product_class</code> | string | Yes | <code>"prototype"</code> or <code>"production"</code> |
| <code>ipc_class</code> | integer | Yes (from fab notes / title block) | <code>1</code>, <code>2</code>, or <code>3</code> (default: <code>2</code>) |
| <code>target_market</code> | string | Yes (from component MPNs) | <code>"hobby"</code>, <code>"consumer"</code>, <code>"industrial"</code>, <code>"medical"</code>, <code>"automotive"</code>, <code>"aerospace"</code> |
| <code>expected_lifetime_years</code> | integer | Yes (market-adjusted) | Product expected lifetime in years |
| <code>operating_temp_range</code> | array of 2 numbers | Yes (market-adjusted) | <code>[min_C, max_C]</code> |
| <code>operating_temp_min</code> | number | — | Alternative to <code>operating_temp_range</code>; can be combined with <code>operating_temp_max</code> |
| <code>operating_temp_max</code> | number | — | Alternative to <code>operating_temp_range</code>; can be combined with <code>operating_temp_min</code> |
| <code>preferred_passive_size</code> | string | Yes (default <code>"0603"</code>) | <code>"0201"</code>, <code>"0402"</code>, <code>"0603"</code>, <code>"0805"</code>, <code>"1206"</code> |
| <code>test_coverage_target</code> | number | Yes (market-adjusted) | <code>0.0</code> to <code>1.0</code> |
| <code>approved_manufacturers</code> | array of strings | No | Restrict to approved manufacturers; empty means no restriction |

**Market-adjusted defaults for auto-detected fields:**

| Market | <code>operating_temp_range</code> | <code>test_coverage_target</code> | <code>expected_lifetime_years</code> |
|--------|----------------------|----------------------|--------------------------|
| hobby / consumer | [-10, 70] | 0.85 | 5 |
| industrial / medical | [-40, 85] | 0.90 | 10 |
| automotive | [-40, 125] | 0.95 | 15 |
| aerospace | [-55, 125] | 0.98 | 20 |

**IPC class auto-detection priority:**
1. Explicit config (<code>"ipc_class"</code> key)
2. PCB fab/user/comments layer text (looks for "IPC-6012 Class N", "IPC Class N", "IPC-6012EM/ES")
3. PCB title block fields
4. Schematic title block fields
5. Inferred from <code>target_market</code> (medical/aerospace → Class 3)
6. Default: Class 2

```jsonc
"design_intent": {
  "product_class": "production",
  "ipc_class": 2,
  "target_market": "consumer",
  "expected_lifetime_years": 7,
  "operating_temp_range": [-10, 60],
  "preferred_passive_size": "0402",
  "test_coverage_target": 0.90,
  "approved_manufacturers": ["Murata", "TDK", "Yageo", "ROHM"]
}
```

---

## Complete Example

Production consumer electronics board targeting the EU market. LCSC primary supplier, IPC Class 2, with suppressions, power rail filtering, BOM config, and branding.

```jsonc
{
  "version": 1,

  // ── Project metadata ────────────────────────────────────────────────
  "project": {
    "name": "Smart Thermostat Controller",
    "number": "STC-200",
    "revision": "B",
    "company": "Acme Devices Ltd.",
    "author": "A. Engineer",
    "market": "eu",                    // CE marking target
    "emc_standard": "cispr-class-b",
    "compliance_market": "eu",
    "ambient_temperature_c": 30        // indoor install, above ambient
  },

  // ── Preferred suppliers (LCSC primary for JLCPCB assembly) ───────────
  "preferred_suppliers": ["lcsc", "digikey"], // v1.2

  // ── BOM conventions ─────────────────────────────────────────────────
  "bom": {                                     // v1.2
    "field_priority": ["LCSC", "MPN", "Digi-Key_PN"],
    "group_by": "mpn"
  },

  // ── Suppressions (additive across config layers) ─────────────────────
  "suppressions": [
    {
      "rule_id": "DC-003",
      "components": ["TP*"],
      "reason": "Test points intentionally unpopulated in production build"
    },
    {
      "rule_id": "EMC-011",
      "nets": ["VBUS_RAW"],
      "reason": "VBUS filtered upstream on power input board; this board sees clean rail"
    },
    {
      "rule_id": "THM-002",
      "components": ["U4"],
      "reason": "U4 (WiFi module) has internal thermal management; Rth_JA from module datasheet used directly"
    }
  ],

  // ── Analysis behavior ────────────────────────────────────────────────
  "analysis": {
    "output_dir": "analysis",
    "retention": 10,
    "auto_diff": true,
    "track_in_git": false,
    "diff_threshold": "minor",         // catch minor changes in CI

    // Power rail tuning (v1.2)
    "power_rails": {
      // Exclude raw/intermediate rails from power tree figures and audit
      "ignore": ["VBUS_RAW", "VBUS_FILT", "BOOT_*"],

      // Highlight unregulated rail for extra scrutiny
      "flag": ["+12V_UNREG"],

      // Correct auto-detected voltages where inference is wrong
      "voltage_overrides": {
        "+3V3_MCU": 3.3,
        "+3V3_RADIO": 3.3,
        "VDDIO_SENS": 1.8
      }
    }
  },

  // ── Design intent ────────────────────────────────────────────────────
  "design_intent": {
    "product_class": "production",
    "ipc_class": 2,
    "target_market": "consumer",
    "expected_lifetime_years": 7,
    "operating_temp_range": [0, 55],   // indoor thermostat, not industrial
    "preferred_passive_size": "0402",
    "test_coverage_target": 0.90,
    "approved_manufacturers": ["Murata", "TDK", "Samsung", "ROHM", "Yageo", "onsemi", "STMicroelectronics"]
  }
}
```

## Quick Field Index

| Field path | Type | v1.2 | Default | Auto-detected |
|------------|------|-------|---------|---------------|
| <code>version</code> | int | — | 1 | — |
| <code>project.name</code> | string | — | — | — |
| <code>project.number</code> | string | — | — | — |
| <code>project.revision</code> | string | — | — | — |
| <code>project.company</code> | string | — | — | — |
| <code>project.author</code> | string | — | — | — |
| <code>project.market</code> | string | — | — | — |
| <code>project.ambient_temperature_c</code> | number | — | 25 | — |
| <code>project.emc_standard</code> | string | — | — | — |
| <code>project.compliance_market</code> | string | — | — | — |
| <code>suppressions</code> | array | — | [] | — |
| <code>preferred_suppliers</code> | array | Yes | [] | — |
| <code>bom.field_priority</code> | array | Yes | — | — |
| <code>bom.group_by</code> | string | Yes | <code>"value+footprint"</code> | — |
| <code>analysis.output_dir</code> | string | — | <code>"analysis"</code> | — |
| <code>analysis.retention</code> | int | — | 5 | — |
| <code>analysis.auto_diff</code> | bool | — | true | — |
| <code>analysis.track_in_git</code> | bool | — | false | — |
| <code>analysis.diff_threshold</code> | string | — | <code>"major"</code> | — |
| <code>analysis.power_rails.ignore</code> | array | Yes | [] | — |
| <code>analysis.power_rails.flag</code> | array | Yes | [] | — |
| <code>analysis.power_rails.voltage_overrides</code> | object | Yes | {} | — |
| <code>design_intent.product_class</code> | string | — | <code>"prototype"</code> | Yes |
| <code>design_intent.ipc_class</code> | int | — | 2 | Yes |
| <code>design_intent.target_market</code> | string | — | <code>"hobby"</code> | Yes |
| <code>design_intent.expected_lifetime_years</code> | int | — | market-adj. | Yes |
| <code>design_intent.operating_temp_range</code> | array[2] | — | market-adj. | Yes |
| <code>design_intent.operating_temp_min</code> | number | — | — | — |
| <code>design_intent.operating_temp_max</code> | number | — | — | — |
| <code>design_intent.preferred_passive_size</code> | string | — | <code>"0603"</code> | Yes |
| <code>design_intent.test_coverage_target</code> | number | — | market-adj. | Yes |
| <code>design_intent.approved_manufacturers</code> | array | — | [] | — |
