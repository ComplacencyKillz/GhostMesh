---
---
# Claude Code Dispatcher Recipe

> **Reference dispatcher for v1.4 Phase 3a.** Satisfies <code>dispatcher-contract.md</code> using Claude Code's <code>Agent</code> tool.

This document tells an orchestrator running inside Claude Code how to dispatch the per-task subagents for an extraction plan. It assumes the orchestrator has read <code>dispatcher-contract.md</code> and the relevant prompt templates.

## Inputs

Two files in <code><cache_dir></code>:

- <code><mpn>.plan.json</code> — the plan written by <code>plan_extraction.py</code>
- <code><mpn>.scout.json</code> — the unwrapped scout output (used to inject metadata if a prompt template needs it; in 3a, only <code>plan.tasks[].pages</code> is needed)

## Workflow

### Step 1 — Confirm scout has run

If <code><mpn>.scout.json</code> does not exist, run the scout first:

1. Read <code>skills/datasheets/prompts/scout.md</code>.
2. Substitute placeholders: <code>{{MPN}}</code> = <code><plan.mpn></code>, <code>{{PDF_PATH}}</code> = <code><plan.pdf_path></code>, <code>{{SCHEMA_PATH}}</code> = <code>skills/datasheets/schemas/scout.schema.json</code>.
3. Invoke <code>Agent</code> with:
   - <code>subagent_type: general-purpose</code>
   - <code>description: "Datasheet scout for <mpn>"</code>
   - <code>prompt</code>: the substituted prompt
4. Capture the agent's JSON output. Validate against <code>skills/datasheets/schemas/scout.schema.json</code>. If valid, write the unwrapped JSON to <code><cache_dir>/<mpn>.scout.json</code> AND a wrapped version to <code><cache_dir>/<mpn>.scout.result.json</code> (status complete, schema_version 1.0, extracted_at now, model_tier B, model_id claude-{current model}, data = the JSON).
5. Then re-invoke <code>plan_extraction.py <mpn> <pdf_path> --use-cached-scout --cache-dir <cache_dir></code> to generate the plan.

### Step 2 — Dispatch per-task subagents in parallel

For each <code>task</code> in <code>plan.tasks</code> with <code>status: "pending"</code> and <code>depends_on: []</code>, dispatch in parallel **in a single Claude Code message containing one <code>Agent</code> tool-call per task**.

For each task:

1. Read the prompt template from <code>task.prompt_template</code> (relative to repo root).
2. Substitute placeholders:
   - <code>{{MPN}}</code> ← <code>plan.mpn</code>
   - <code>{{PDF_PATH}}</code> ← <code>plan.pdf_path</code>
   - <code>{{PAGES}}</code> ← human-readable formatted list (e.g. <code>task.pages = [5,6,13,14,15]</code> → <code>"5, 6, 13–15"</code>)
   - <code>{{SCHEMA_PATH}}</code> ← <code>task.schema</code>
3. Invoke <code>Agent</code> with:
   - <code>subagent_type: general-purpose</code>
   - <code>description: "Datasheet <task.subagent_role> extractor for <mpn>"</code>
   - <code>prompt</code>: the substituted prompt (which includes the page list as part of the agent's instructions and the schema path as the validation contract)

**All three (base, pinout, regulator) for LM2596-ADJ go in one message.** Claude Code dispatches them concurrently. Wait for all to return.

### Step 3 — Wrap and validate each agent output

For each returned agent output:

1. Parse the JSON from the agent's response. If the agent wrapped it in markdown code fences or surrounding prose, strip those (the prompts say "no prose, no fences" but be defensive).
2. Run JSON Schema validation against <code>task.schema</code> (use <code>python -c "import json,jsonschema; ..."</code> or call <code>merge_results.py</code> indirectly).
3. Build the wrapped result:
   ```json
   {
     "task_id": "<task.task_id>",
     "schema_version": "<schema's x-schema-version>",
     "status": "complete" or "failed",
     "extracted_at": "<now ISO-8601>",
     "model_tier": "<task.tier>",
     "model_id": "claude-<current Claude Code model>",
     "data": <agent JSON, or null>,
     "error": "<validation error message>"  // only if status == failed
   }
<pre><code>
4. Write to <code><cache_dir>/<mpn>.<task_id>.result.json</code>.

### Step 4 — Run merge_results.py

<pre><code>
python3 skills/datasheets/scripts/merge_results.py <mpn> --cache-dir <cache_dir>
</code></pre>

If exit code is 0: extraction is complete; <code><cache_dir>/<mpn>.json</code> is the canonical cache file. Done.

If exit code is 1: at least one task is in <code>failed</code> status. Read stderr for the specific task IDs.

### Step 5 — Retry once on hard failure

For each failed task:

1. Read the existing <code><mpn>.<task_id>.result.json</code> to get the <code>error</code> message.
2. Read <code>task.prompt_template</code> again, substitute placeholders, **and append**:

</code></pre>
   ## Previous attempt failed

   Your previous output failed validation with this error:
   <error>

   Re-read the relevant pages and produce a corrected output. Pay particular attention to: <hint based on error category — e.g. "missing required field topology", "min > max in VIN_max">.
<pre><code>

3. Invoke <code>Agent</code> again with the augmented prompt.
4. Wrap and write the new result file (overwriting the failed one).

### Step 6 — Re-merge with --retry-failed

<pre><code>
python3 skills/datasheets/scripts/merge_results.py <mpn> --cache-dir <cache_dir> --retry-failed
</code></pre>

Exit code 0 always (this is the second-and-final merge). Tasks that succeeded merge cleanly; tasks that still failed are partial-merged with <code>{"_extraction_failed": true, "reason": ...}</code>.

## Concurrency note

Claude Code's <code>Agent</code> tool supports parallel tool-call dispatch within a single message. Issuing all three Phase 3a tasks (base, pinout, regulator) in one message gives ~3× speedup over serial. If context bloat becomes an issue (long conversations after multiple PDFs), fall back to two messages: <code>[base, pinout]</code> then <code>[regulator]</code>.

## Cost ledger (optional)

If you want to record cost data for analysis, append one JSONL entry per <code>Agent</code> invocation to <code><cache_dir>/_cost_ledger.jsonl</code>:

<pre><code>
{"run_id": "20260425T100000Z-a1b2c3", "mpn": "LM2596-ADJ", "task_id": "scout", "tier": "B", "model_id": "claude-sonnet-4-6", "tokens_in": null, "tokens_out": null, "cost_usd": null, "success": true, "extracted_at": "2026-04-25T10:00:30Z"}
</code></pre>

Tokens and cost are unavailable from inside Claude Code's <code>Agent</code> tool — leave them null in v1.4. v1.5 SDK dispatcher populates them.

## Failure modes to watch for

- **Agent returns prose with code fences.** Strip the fences before parsing.
- **Agent emits non-canonical units** (e.g. <code>unit: "µF"</code> instead of <code>unit: "F"</code> with value 4.7e-4). The schema accepts µF as a string but the verifier and downstream consumers expect F. Treat as a hard failure (schema validation should catch via <code>datasheet_verify.py</code> v1.4 extensions running in the harness gate).
- **Agent invents pin numbers** in the regulator extractor that don't appear in pinout. The verifier catches this.
- **Family-PDF disambiguation drift**: agent extracts a different variant's spec. Catches via sanity vector diff.
- **Page list not honored**: agent reads pages outside <code>task.pages</code>. The prompt instructs "focus on these pages"; if the agent ranges further, that's allowed (the constraint is "not less than these pages", not "only these pages").

## Example conversation flow (LM2596-ADJ)

<pre><code>
USER: Run Phase 3a extraction for LM2596-ADJ from /path/to/LM2596.pdf

ORCHESTRATOR:
  - Run plan_extraction (which would dispatch scout — but cached-scout flow:
    first invoke scout subagent via Agent tool, write scout.json, then
    invoke plan_extraction --use-cached-scout)
  - [Single Agent call: scout]
  - Write LM2596-ADJ.scout.json + LM2596-ADJ.scout.result.json
  - Run plan_extraction.py LM2596-ADJ /path/to/LM2596.pdf --use-cached-scout
  - [Single message with three parallel Agent calls: base, pinout, regulator]
  - Wrap and write 3 result files
  - Run merge_results.py LM2596-ADJ
  - If any failures → retry once, re-merge with --retry-failed
  - Hand off to harness for 4-check gate
</code></pre>

---

## Phase 4 addendum: dispatching review tasks

The <code>design_context</code> task (v2.0) reuses this same dispatcher contract per spec §4.5. Key differences:

- Tasks have <code>task_type: "review"</code> (vs <code>"extraction"</code> for Phase 3 datasheet tasks).
- The design_context task lives at <code>skills/kicad/review/prompts/design_context.md</code>. The reviewer task is retired in v2.0 (superseded by the Deep Review pass — see <code>skills/kicad/references/deep-review.md</code>).
- Result schemas live under <code>skills/kicad/review/schemas/{design_context,review_annotations}.schema.json</code>.
- Result paths are <code>analysis/<artifact>.json</code> (NOT <code><mpn>.<task>.result.json</code> — review outputs are run-level, not MPN-level).

### Task → Subagent mapping

When you see a task with <code>task_type: "review"</code>:

| <code>task_id</code> | Tier | Subagent prompt |
|-----------|------|----------------|
| <code>design_context</code> | B (cheaper) | <code>skills/kicad/review/prompts/design_context.md</code> |

Same dispatch primitive (Claude Code <code>Task</code> tool); same output-validation contract (validate against <code>result_schema</code> after subagent returns); same retry semantics (one retry on hard fail with error context).

### Merge after design_context task completes

Once the design_context task has written its result file, invoke:

<pre><code>
python3 skills/kicad/review/scripts/merge_annotations.py \
    --raw-dir analysis/ \
    --review analysis/review_annotations.json \
    --merged-dir analysis/merged/
</code></pre>

This applies overlays to a copy of each raw analyzer JSON. The raw files remain unmodified.
