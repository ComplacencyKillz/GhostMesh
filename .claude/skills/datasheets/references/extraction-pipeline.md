---
---
# Extraction Pipeline — Full Procedure

Full extraction-pipeline procedure. The SKILL.md main flow points here; scripts do the orchestration — this is depth-on-demand, not a required read.

---

## Extraction workflow

**v1.4 pipeline (current, used for all new extractions):**
1. <code>plan_extraction.py</code> builds an orchestration plan JSON.
2. Scout subagent inspects the PDF (TOC, headings) and emits per-MPN scout audit file.
3. Category extractor prompts (base, pinout, regulator, …) run per Phase 2 dispatcher recipe.
4. <code>merge_results.py</code> validates per-task result files against schemas and merges into <code><project>/datasheets/extracted/<MPN>.json</code>.
5. Three-dimension quality score lives at <code>facts.extraction.quality_score</code>.
6. Consumers query via <code>lookup(mpn, cache_dir)</code> or via the v1.3 compat helpers in <code>datasheet_features.py</code>.

**v1.3 legacy pipeline (read-only in v1.4):**
1. User runs an analyzer or requests extraction.
2. Skill checks the cache (<code><project>/datasheets/extracted/<MPN>.json</code>).
3. On cache miss / stale / low score: Claude reads selected PDF pages and extracts structured data.
4. Extraction is scored; if score ≥ 6.0, cached.
5. Consumers query via <code>datasheet_features.py</code>.

For dispatcher dispatch recipes and subagent recipes, see <code>references/dispatch-claude-code.md</code> and <code>references/dispatcher-contract.md</code>.
