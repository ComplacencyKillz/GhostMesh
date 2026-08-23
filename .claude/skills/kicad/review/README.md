---
---
# <code>skills/kicad/review/</code> — Deep Review Sub-component (v2.0)

This is an internal sub-component of the kicad skill. It is NOT a standalone skill (no nested SKILL.md — confirmed unsupported across Claude Code, Codex, Gemini CLI). Referenced from <code>skills/kicad/SKILL.md</code> as a progressive-disclosure link.

## Purpose

This directory hosts the v2.0 Deep Review infrastructure:

- **(a)** The Deep Review evidence gate + <code>deep_review.schema.json</code> (spec §3.C/§3.D): per-IC LLM datasheet comparison with durable, evidence-linked findings in <code>analysis/deep_review.json</code>.
- **(b)** The optional <code>design_context</code> subagent input (spec §3.B): reads schematic + BOM + design intent, emits <code>design_context.json</code>. Used as an optional input to the Deep Review pass.
- **(c)** The annotation merge machinery (optional detector-finding triage): <code>merge_annotations.py</code> validates <code>review_annotations.json</code>, applies <code>llm_review</code> overlays to raw analyzer findings, and verifies HI-3 strip round-trip. Authority caps removed in v2.0 — a <code>suppressed</code> annotation now always applies its overlay. Quarantined findings remain visible in the merge report.
- **(d)** <code>_mini_jsonschema.py</code>: stdlib-only JSON Schema Draft 2020-12 validator (keyword subset used by Layer 2 schemas). No third-party <code>jsonschema</code> dep at runtime.

## Structure

| Path | Purpose |
|------|---------|
| <code>prompts/design_context.md</code> | Subagent prompt: read schematic + BOM + design intent, emit design_context.json |
| <code>schemas/design_context.schema.json</code> | JSON Schema for design context output |
| <code>schemas/review_annotations.schema.json</code> | JSON Schema for review annotations |
| <code>schemas/deep_review.schema.json</code> | JSON Schema for Deep Review evidence-gated findings |
| <code>scripts/build_review_plan.py</code> | Emits 1-task plan JSON for design_context dispatch |
| <code>scripts/merge_annotations.py</code> | Validates + applies overlays to raw analyzer JSONs → <code>analysis/merged/<analyzer>.json</code> |
| <code>scripts/validate_review.py</code> | Standalone CLI for review_annotations.json validation |
| <code>scripts/deep_review_gate.py</code> | Evidence gate: validates deep_review.json, writes durable findings |
| <code>scripts/run_phase4_exercise.py</code> | Orchestrates the end-to-end fixture exercise |
| <code>fixtures/*.example.json</code> | Round-trip fixtures for schema contract tests |

## Surviving invariants (v2.0)

- **HI-1:** Layer 1 findings are immutable — detector-owned fields are never mutated by any overlay machinery.
- **HI-3:** <code>strip_llm_overlays(merged)</code> == raw input, byte-identical. The merge is always reversible.
- **HI-6:** <code>reviewer_observations[]</code> lives in <code>review_annotations.json</code>, never merged into <code>findings[]</code>.
- **No-LLM run yields identical analyzer output:** stripping <code>llm_*</code> fields recovers the deterministic baseline.
- **Stable finding identity:** <code>finding_id</code> is stable across runs; this invariant extends to Deep Review findings (<code>deep_review:<12-hex></code> form).
- **No hidden suppression:** quarantined/suppressed annotations are visible in the merge report; nothing is silently discarded.

## Retired in v2.0 (spec §5)

The following v1.4 Layer 2 cage constraints are removed. Trust now comes from the Deep Review evidence gate, not permission rules:

- **Overlay-only constraint (HI-2 label):** the merge still only writes <code>llm_review</code> siblings, but this is now a design property, not a cage rule.
- **Observations-are-not-findings (HI-6 framing):** still true mechanically; but no longer framed as a restriction.
- **All authority caps and suppression rules (HI-8/HI-9):** <code>reviewer_observations[]</code> no longer has a <code>maxItems</code> cap; <code>confidence</code> is no longer capped at <code>medium</code>; <code>severity</code> is no longer capped at <code>warning</code>; suppressing an <code>error</code>-severity or <code>datasheet-backed</code> finding now applies normally; the 30% suppression rate cap is removed.
- **Severity tuning matrix:** <code>severity_tuning.json</code> and <code>severity_tuning.schema.json</code> deleted; <code>make_finding(design_context=...)</code> accepts the parameter for call-site compatibility but ignores it.
- **Layer 2 reviewer prompt:** archived to <code>old/layer2/reviewer.md</code> (gitignored on disk).
