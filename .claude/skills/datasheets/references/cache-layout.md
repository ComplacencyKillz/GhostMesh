# Datasheet Cache Layout (v1.4)

Reference doc describing the on-disk convention for datasheet extractions.
Consolidates details scattered across Tracks 2.1–2.6 so Phase 3 extraction
implementers and future category-schema authors have a single place to
check.

## Directory Structure

```
<project>/
  datasheets/
    manifest.json                       # Tier 1 PDF SHA dedup (Track 2.1 schema).
    LM2596-ADJ.pdf                      # Source PDFs — one per unique SHA.
    yageo-rc0603.pdf
    extracted/                          # Per-MPN extraction cache.
      LM2596-ADJ.json                   # Per-MPN fact envelope (Track 2.1).
      LM2596-ADJ.plan.json              # Orchestration plan audit (Phase 3).
      LM2596-ADJ.scout.json             # Scout subagent output (Phase 3).
      RC0603FR-071KL.variant.json       # Per-MPN variant overrides (v1.5).
      RC0603FR-0750KL.variant.json
      _families/                        # Reserved for v1.5 Tier 2 dedup (spec §14).
        yageo-rc0603.family.json        # Canonical family extraction (v1.5).
```

## File Naming

### Per-MPN cache files: <code><sanitized_mpn>.json</code>

<code>lookup(mpn, cache_dir=...)</code> resolves to <code>cache_dir / f"{sanitize_mpn(mpn)}.json"</code>.
Sanitization rules (<code>skills/datasheets/scripts/datasheet_lookup.py</code>):

- Strip leading/trailing whitespace.
- Replace any character NOT in <code>[A-Za-z0-9_-]</code> with <code>_</code>.
- No hash suffix. Two MPNs that sanitize to the same string collide; this
  is acceptably rare in practice given real MPN character sets.

Examples:
- <code>LM2596-ADJ</code> → <code>LM2596-ADJ.json</code>
- <code>STM32F103C8T6</code> → <code>STM32F103C8T6.json</code>
- <code>STM32/F103</code> → <code>STM32_F103.json</code>
- <code>LM 2596</code> → <code>LM_2596.json</code>

### Source PDFs: <code><part_or_family>.pdf</code>

Lives at <code>datasheets/<filename>.pdf</code>. The per-MPN cache file's
<code>source.local_path</code> stores the filename relative to <code>datasheets/</code>. A
<code>datasheets/</code> prefix in <code>local_path</code> is tolerated for v1.3 legacy caches
(see Track 2.3's <code>_resolve_pdf_path</code>) — new extractions write without
the prefix.

### Manifest (<code>datasheets/manifest.json</code>)

Single file at the <code>datasheets/</code> level. Two sections:

1. **<code>pdfs</code>** — keyed by <code>sha256:<hex></code>, values are <code>{path, mpns[], source_url, is_family}</code>. Tier 1 SHA dedup: when a PDF is downloaded, the downloader checks if its SHA is already a key; if so, the new MPN is appended to the existing entry's <code>mpns[]</code> instead of duplicating the PDF.
2. **<code>extractions</code>** — legacy v1.3 cache index, retained for <code>datasheet_features.py</code> dual-cache-read fallback (Track 2.5). New v1.4 extractions don't write to this section — they write directly to <code><MPN>.json</code> under <code>extracted/</code> and reference the PDF via the <code>pdfs</code> section.

Full schema: <code>skills/datasheets/schemas/manifest.schema.json</code>. Required <code>pdfs</code> entry fields: <code>path</code>, <code>mpns</code>. Optional: <code>source_url</code>, <code>is_family</code>.

### Orchestration audit files (Phase 3)

<code><MPN>.plan.json</code> and <code><MPN>.scout.json</code> are written by the Phase 3
<code>datasheets sync</code> extraction pipeline alongside the main <code><MPN>.json</code>.
They persist the scout subagent output and the per-task orchestration
plan for audit and replay.

<code><MPN>.json</code> references these side-files via <code>extraction.plan_ref</code>
(relative filename). A missing plan or scout file does not invalidate
the cache — those files are audit metadata, not required for
consumption by <code>lookup()</code>.

### <code>_families/</code> subdirectory (reserved for v1.5)

Reserved for v1.5 Tier 2 family extraction (spec §14). In v1.4 this
directory is **not written to by any code path**. Its presence — if a
v1.5 corpus is mixed with v1.4 readers — does not interfere with
<code>lookup()</code> or any other v1.4 tooling.

**Why the leading underscore:** distinguishes the reserved directory
from per-MPN files that could theoretically sanitize to the same name.
<code>_families</code> is NOT a valid sanitized MPN output (sanitizer preserves
underscore, so <code>_families</code> could in principle be produced by an MPN
literally named <code>_families</code>; the underscore prefix is a soft-social
reservation, not a structural guarantee). The Track 2.6 regression
test <code>test_lookup_ignores_families_subdirectory_coexisting_with_cache_files</code>
locks the invariant that this edge case does not break <code>lookup()</code>.

**v1.5 layout preview:**

```
extracted/
  _families/
    yageo-rc0603.family.json        # Canonical family extraction.
  RC0603FR-071KL.variant.json       # Per-MPN variant overrides.
  RC0603FR-0750KL.variant.json
```

A v1.5 <code>lookup()</code> call for <code>RC0603FR-071KL</code> will:
1. Read <code>RC0603FR-071KL.variant.json</code>.
2. Follow <code>source.family_ref</code> (currently always null in v1.4) to locate the family file in <code>_families/</code>.
3. Merge the family facts with the variant overrides.
4. Return a single merged <code>DatasheetFacts</code>.

v1.4 has no merging logic — <code>source.family_ref</code> is always <code>None</code> in
every v1.4 extraction. Future extraction pipelines must preserve this
until v1.5 ships the Tier 2 reader.

## Cache Invalidation

A per-MPN cache entry is considered **stale** when any of these hold
(paraphrased from spec §8 + Track 2.3 <code>lookup()</code> staleness logic):

1. **PDF sha256 mismatch** — <code>source.sha256</code> in the cache JSON does not
   match the sha256 of the PDF at <code>datasheets/<local_path></code>. Detected by
   <code>lookup()</code>'s <code>CacheContext.stale_reason = "pdf_hash_mismatch"</code>.
2. **PDF missing** — <code>source.local_path</code> is null, OR the referenced PDF
   doesn't exist on disk. Detected by <code>lookup()</code>'s
   <code>CacheContext.stale_reason = "pdf_missing"</code>.
3. **Schema version major-bumped** — when <code>base.schema.json</code> or a
   category extension's major version changes, cached extractions of
   that section become stale. v1.4 does not enforce this at read time
   (consumers opt in via <code>min_schema</code> per spec §13); Phase 3 extraction
   will re-run when it detects a mismatch.
4. **Quality score below threshold** — extraction-act-time check
   (<code>extraction.quality_score</code> < project-configured threshold).
   v1.4 does not enforce at read time.
5. **Manual <code>--force</code>** — Phase 3 <code>datasheets sync --force</code> re-extracts
   ignoring staleness.

<code>lookup()</code> in v1.4 only surfaces PDF-related staleness (#1, #2). The
other triggers are extraction-lifecycle concerns for Phase 3.

## Stale Cache Handling

<code>lookup()</code> does **not** automatically purge or regenerate stale caches.
Staleness is an advisory signal exposed via <code>DatasheetFacts.stale</code>;
consumers decide what to do:

- Phase 4 detectors: consult <code>ds.stale</code> and downgrade finding confidence
  accordingly.
- Phase 3 <code>datasheets sync</code>: treat staleness as a trigger for
  re-extraction.
- v1.3 compat wrappers (<code>datasheet_features.py</code>, Track 2.5): ignore
  staleness — v1.3 API returns the dict either way; consumers got this
  from v1.3 too.

## Static Examples vs Runtime Cache

Two distinct directories that look similar but serve different purposes:

- **<code>skills/datasheets/examples/<mpn>.json</code>** (in this repo) — static
  schema documentation, one canonical merged extraction per Phase 3b
  category (regulator, crystal, transistor, opamp, mcu, diode). These
  files are **not read by <code>lookup()</code>** at runtime — they exist solely
  to make the v1.4 schemas self-documenting via concrete instances.
  Six MPNs: <code>lm2596-adj</code>, <code>abm8g-106-12.000mhz-t</code>, <code>irlml6344</code>,
  <code>lm358</code>, <code>stm32f103c8t6</code>, <code>mbrs540t3g</code>.
- **<code><user-project>/datasheets/extracted/<MPN>.json</code>** — runtime cache,
  populated by users running <code>datasheets sync</code> against their own
  schematics. <code>lookup(mpn, cache_dir=<user-project>/datasheets/extracted)</code>
  reads from here.

The Phase 3a/3b extraction audit trail (per-stage <code>.scout.*</code>,
<code>.base.*</code>, <code>.<category>.*</code>, <code>.pinout.*</code>, <code>.plan.*</code> files for the six
canonical MPNs) lives in the harness repo
(<code>kicad-happy-testharness</code>) as test fixtures, not in this product
repo. This product repo only ships the final merged JSONs as static
examples.

## Related Tracks

- **Track 2.1** — JSON Schema contracts for per-MPN files, pinout,
  spec_value, base, regulator, extraction envelope, manifest.
- **Track 2.2** — Typed Python dataclasses matching Track 2.1 schemas.
- **Track 2.3** — <code>lookup(mpn, cache_dir=...)</code> facade + MPN sanitization
  + staleness detection.
- **Track 2.5** — Dual-cache-read layer in <code>datasheet_features.py</code>
  preserving v1.3 API compatibility.
- **Phase 3 (planned)** — Extraction pipeline writes new entries; reads
  <code>manifest.json</code> for dedup; populates <code><MPN>.plan.json</code> /
  <code><MPN>.scout.json</code> audit trail.
- **v1.5 (planned)** — Tier 2 family extraction consumes <code>_families/</code>.
