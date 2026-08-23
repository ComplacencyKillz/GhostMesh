---
name: datasheets
description: Extract structured specifications from electronic component datasheet PDFs — pinouts, electrical characteristics, peripherals, topology, and features. Cache extractions per project for consumption by schematic and PCB analyzers. Primary consumer infrastructure for <code>kicad</code>, <code>emc</code>, <code>spice</code>, and <code>thermal</code> analyzers. Use this skill whenever the user asks to extract, verify, or read specs from a component datasheet; when analyzers need verified IC knowledge (EN pin thresholds, PG presence, USB peripheral speed); or when a review mentions datasheet coverage, extraction quality, or per-MPN specifications. Also triggers on "extract this datasheet", "what are the specs for MPN X", "verify datasheet extraction", or "check pin functions for part Y".
---

# Datasheets Skill

## Related Skills

| Skill | Relationship |
|-------|--------------|
| <code>digikey</code> / <code>mouser</code> / <code>lcsc</code> / <code>element14</code> | **Producers** — download the PDFs under <code><project>/datasheets/</code> that this skill extracts from |
| <code>kicad</code> | **Primary consumer** — VM-001/PU-001/FS-001/PP-001/LR-001/XT-001 + Phase 4b lookup detectors (AM-001/OV-001/TJ-001/FT-001/EX-001) query extractions via <code>lookup(mpn)</code> for verified-IC knowledge |
| <code>emc</code> | **Consumer** — switching-frequency, package-Rθ_JA, and operating-voltage data sharpen EMC heuristics |
| <code>spice</code> | **Consumer** — SPICE model presence + IBIS data feed simulation-readiness checks |
| <code>thermal</code> | **Consumer** — package Rθ_JA + junction temperature limits drive Tj estimates (TS-001..TJ-001) |
| <code>bom</code> | **Indirect** — coverage of structured extractions affects BOM verification confidence |

**Handoff guidance:** This skill is consumer infrastructure. The typical flow is <code>distributor skill downloads PDF → datasheets skill extracts → analyzer skill queries</code>. Use this skill directly when (a) the user asks to extract or verify a specific MPN, (b) an analyzer reports <code>trust_level: low</code> and the gap is per-MPN extraction quality, or (c) a new MPN was added to the BOM and downstream detectors should pick up its verified specs. Don't run this skill in isolation if the user just wants a design review — call it from the kicad workflow at the "Sync datasheets" step instead.

## Purpose

Extract structured, machine-readable specifications from component datasheet PDFs and make them available to analyzer skills. Works on whatever PDFs are downloaded under <code><project>/datasheets/</code> (downloads are owned by distributor skills like <code>digikey</code>, <code>mouser</code>, <code>lcsc</code>, <code>element14</code>).

## Scope

This skill owns:
- **Extraction schemas** — canonical JSON structures for per-MPN specs. v1.4 ships 6 JSON Schema Draft 2020-12 schemas under <code>schemas/</code> (<code>base</code>, <code>pinout</code>, <code>spec_value</code>, <code>regulator</code>, <code>extraction</code>, <code>manifest</code>) plus 5 v1.4 category extensions (diode, transistor, opamp, mcu, crystal). v1.3 cache format (<code>EXTRACTION_VERSION</code> in <code>scripts/datasheet_extract_cache.py</code>) is still read for compat.
- **Typed access layer (v1.4)** — <code>datasheet_types/</code> package exposes <code>DatasheetFacts</code>, <code>SpecValue</code>, <code>Pin</code>, <code>Pinout</code>, <code>lookup()</code>, <code>best()</code>, <code>trusted()</code>, <code>has_data()</code>. Recommended for all new consumers.
- **PDF page selection** — heuristics to pick pages most likely to contain pinouts, e-chars, applications, SPICE models.
- **Quality scoring** — v1.4 uses a three-dimension rubric (pinout completeness, base completeness, category-extension completeness, 0–100 scale). v1.3 5-dimension weighted rubric still applies to legacy caches.
- **Consumer APIs** — <code>scripts/datasheet_lookup.py</code> for v1.4 typed access; <code>scripts/datasheet_features.py</code> for the v1.3 dict-shaped helpers (<code>get_regulator_features</code>, <code>get_mcu_features</code>, <code>get_pin_function</code>) — the v1.3 helpers dual-read v1.4 caches and translate to v1.3 dict shape for legacy detector code. Sunset planned for v1.6.
- **Verification** — <code>datasheet_verify.py</code> (v1.3, schema-vs-usage cross-check) plus <code>datasheet_verify_v14_extraction</code> (v1.4, power_domain references resolve, recommended ≤ absolute, regulator pin references exist).

## Non-goals

- **No PDF downloading.** That is owned by distributor skills (<code>digikey</code>, <code>mouser</code>, <code>lcsc</code>, <code>element14</code>).
- **No global library.** Each project's extractions live in <code><project>/datasheets/extracted/</code>. There is no shared cross-project cache.

## Cache location

```
<project>/
  design.kicad_sch
  datasheets/
    TPS61023DRLR.pdf        # downloaded by distributor skills
    extracted/
      manifest.json         # extraction manifest (legacy name: index.json)
      TPS61023DRLR.json     # structured extraction (this skill's output)
```

## Reference guides

- <code>references/extraction-schema.md</code> — canonical schema, every field defined
- <code>references/field-extraction-guide.md</code> — how to find each field in datasheets from common vendors (TI, ST, NXP, Espressif, Microchip)
- <code>references/quality-scoring.md</code> — rubric details, score thresholds
- <code>references/consumer-api.md</code> — how kicad/emc/spice/thermal consume extractions
- <code>references/cache-layout.md</code> — v1.4 cache directory convention (per-MPN files, <code>_families/</code> reservation, staleness rules)

## Entry-point scripts

- <code>scripts/datasheet_extract_cache.py</code> — v1.3 cache manager, resolver, indexer
- <code>scripts/datasheet_page_selector.py</code> — page selection heuristics (used by both v1.3 and v1.4 pipelines)
- <code>scripts/datasheet_score.py</code> — v1.3 extraction quality scoring
- <code>scripts/datasheet_verify.py</code> — cross-check extraction vs schematic usage (v1.3 + v1.4 <code>verify_v14_extraction</code> mode)
- <code>scripts/datasheet_lookup.py</code> — **v1.4** typed <code>lookup(mpn) → DatasheetFacts</code> facade with staleness detection
- <code>scripts/datasheet_features.py</code> — v1.3 consumer helper API (dual-reads v1.4 caches via <code>_derive_*_v14</code> translators)
- <code>scripts/plan_extraction.py</code> — **v1.4** orchestration plan generator (Phase 3 extraction pipeline)
- <code>scripts/merge_results.py</code> — **v1.4** per-task result validator + merger
- <code>datasheet_types/</code> — **v1.4** typed access layer package (<code>DatasheetFacts</code>, <code>SpecValue</code>, <code>Pin</code>, <code>Pinout</code>, <code>lookup</code>, <code>best</code>, <code>trusted</code>, <code>has_data</code>)

## Extraction workflow

Run <code>python3 skills/datasheets/scripts/plan_extraction.py <project></code> to generate an orchestration plan, then <code>merge_results.py</code> to validate and merge per-task outputs. Full scout→plan→dispatch→merge procedure: [<code>references/extraction-pipeline.md</code>](references/extraction-pipeline.md).

## Consuming extractions (v1.4 typed API)

The recommended consumer surface is the typed <code>lookup(mpn, cache_dir=...)</code> facade plus the trust-gating helpers from <code>datasheet_types</code>. Import like:

```python
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent.parent / "datasheets"))
from datasheet_types import lookup, has_data, best, trusted

# Returns Optional[DatasheetFacts]. None on cache miss / stale PDF / low quality.
facts = lookup("TPS61023DRLR", cache_dir=pathlib.Path("datasheets/extracted"))
if facts is None:
    return  # heuristic-only path; no datasheet evidence available

# Field-level trust gating — every SpecValue list runs through has_data() / best() / trusted().
pu_range = facts.base.recommended_pullup_range  # Optional[list[SpecValue]]
if has_data(pu_range):
    # Most-trusted single value (first SpecValue meeting threshold, preserves extractor order).
    rec = best(pu_range, min_confidence="medium")  # Optional[SpecValue]
    if rec is not None and rec.min is not None:
        ...  # use rec.min, rec.max, rec.typ, rec.unit, rec.evidence.{page,section,confidence}

# All SpecValues at threshold (for multi-value fields like absolute_max).
hi_conf = trusted(facts.base.absolute_max.get("VDD", []), min_confidence="high")
```

**Defensive patterns** (mirrors <code>kicad/SKILL.md</code> § "Probing Analyzer JSON"):

- <code>lookup()</code> returns <code>None</code> on cache miss, stale PDF (PDF newer than extraction), or quality score below the configured floor. Always guard with <code>if facts is None: return</code>.
- Category extensions are optional on <code>DatasheetFacts</code>. <code>facts.regulator</code> is <code>None</code> when the part isn't in the <code>regulator</code> category — check before dereferencing.
- SpecValue lists can be <code>None</code> (field not extracted), <code>[]</code> (extracted but empty), or <code>list[SpecValue]</code>. <code>has_data()</code> collapses the first two to <code>False</code>; pair with <code>best()</code> / <code>trusted()</code> for confidence gating.
- <code>SpecValue.min</code> / <code>.max</code> / <code>.typ</code> are each <code>Optional[float]</code>. A SpecValue carrying only <code>typ</code> (no range) makes <code>></code> / <code><</code> comparisons against <code>.min</code> / <code>.max</code> raise <code>TypeError</code> — guard with explicit <code>is not None</code> chains on every numeric access.
- <code>confidence</code> is one of <code>"low"</code> / <code>"medium"</code> / <code>"high"</code>. Calling <code>best()</code> / <code>trusted()</code> with any other string raises <code>ValueError</code>.

## v1.3 compat shim

Legacy detectors still call <code>get_regulator_features(mpn)</code> / <code>get_mcu_features(mpn)</code> / <code>get_pin_function(mpn, pin)</code> from <code>scripts/datasheet_features.py</code>. These dual-read v1.4 caches and translate to the v1.3 dict shape. Sunset planned for v1.6 — new code should use <code>lookup()</code> directly.

## When to trigger this skill

- **Immediately after downloading datasheets** via <code>sync_datasheets_digikey.py</code>, <code>sync_datasheets_lcsc.py</code>, or equivalent. Without extraction, IC-aware checks (VM-001 rail voltage, PS-001 power-good, PR-004 USB, DP-002 USB speed classification) fall back to heuristics on unknown ICs.
- **Before running analyzers on a new project** where datasheets are present but <code>datasheets/extracted/</code> is empty — the analyzers won't produce the extractions themselves.
- **When a review flags low trust level** due to missing manufacturer evidence: extracting the ICs referenced by power regulators, MCUs, and high-speed peripherals typically flips <code>trust_level: low</code> → <code>mixed</code> or <code>high</code>.
- **When a user asks for pin verification** ("verify U1 pin names match datasheet") — this skill's cached extraction is the authoritative source.
