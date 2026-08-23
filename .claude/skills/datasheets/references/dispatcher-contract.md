---
---
# Datasheet Extraction Dispatcher Contract

> **Status:** Phase 3a (v1.4). The contract is stable and applies to every dispatcher implementation: <code>dispatch-claude-code.md</code> (v1.4), <code>dispatch-codex.md</code>, <code>dispatch-gemini.md</code>, <code>extract.py</code> SDK runner, canned harness dispatcher (all v1.5+).

A **dispatcher** is the swappable layer between <code>plan_extraction.py</code> and <code>merge_results.py</code> in the Phase 3a extraction pipeline. Its job: read <code><mpn>.plan.json</code>, run each per-task subagent under its native LLM-dispatch primitive, write per-task wrapped result files. Python (plan_extraction + merge_results) is stdlib-only and platform-agnostic; the dispatcher is the only agent-specific component.

## Inputs

The dispatcher reads:

- <code><cache_dir>/<mpn>.plan.json</code> — <code>plan.schema.json</code> shape
- <code><cache_dir>/<mpn>.scout.json</code> — <code>scout.schema.json</code> shape (for prompt placeholder injection)
- The PDF at <code><plan.pdf_path></code> (each subagent reads it directly; the dispatcher does not read PDF content)
- Each <code>task.prompt_template</code> (Markdown with <code>{{MPN}}</code>, <code>{{PDF_PATH}}</code>, <code>{{PAGES}}</code>, <code>{{SCHEMA_PATH}}</code> placeholders)
- Each <code>task.schema</code> (JSON Schema for validation)

## Outputs

Per task: <code><cache_dir>/<mpn>.<task_id>.result.json</code> — the **wrapped result file**.

<pre><code>
{
  "task_id": "regulator",
  "schema_version": "0.3",
  "status": "complete",
  "extracted_at": "2026-04-25T11:00:00Z",
  "model_tier": "B",
  "model_id": "claude-sonnet-4-6",
  "data": { ... }
}
</code></pre>

<code>status</code> ∈ <code>{"complete", "failed"}</code>. Other fields:

- <code>task_id</code>: matches <code>task.task_id</code> exactly.
- <code>schema_version</code>: schema version of the task's output (e.g. <code>"0.3"</code> for regulator). Read from the schema's <code>x-schema-version</code> field if present; otherwise <code>"1.0"</code>.
- <code>extracted_at</code>: ISO-8601 UTC timestamp.
- <code>model_tier</code>: <code>A</code> or <code>B</code> per <code>task.tier</code>.
- <code>model_id</code>: opaque identifier of the LLM that produced the output.
- <code>data</code>: the subagent's output object (or <code>null</code> on failure).
- <code>error</code> (failed only): string describing why the task failed.

## Contract

| | The dispatcher |
|---|---|
| **MUST** | For each task in <code>plan.tasks</code> where <code>status: "pending"</code>: instantiate the subagent with <code>task.prompt_template</code> (placeholders substituted) and the task's PDF page list, capture subagent output, write the wrapped result file to <code><cache_dir>/<mpn>.<task_id>.result.json</code>. |
| **MUST** | Set <code>status: "complete"</code> if subagent output passes JSON-schema validation against <code>task.schema</code>; set <code>status: "failed"</code> with an <code>error</code> field if validation fails or the subagent reports an explicit error. |
| **MUST** | Respect <code>task.depends_on</code> — never dispatch a task whose dependencies haven't all reached <code>status: "complete"</code>. (Phase 3a tasks all have <code>depends_on: []</code>, so this is a future-proofing constraint.) |
| **MUST** | Be idempotent — if a result file already exists with <code>status: "complete"</code>, skip that task (resume-safe). |
| **MUST** | Substitute prompt placeholders: <code>{{MPN}}</code> ← <code>plan.mpn</code>, <code>{{PDF_PATH}}</code> ← <code>plan.pdf_path</code>, <code>{{PAGES}}</code> ← <code>task.pages</code> (formatted as a human-readable list, e.g. "5, 6, 13–15"), <code>{{SCHEMA_PATH}}</code> ← <code>task.schema</code>. |
| **MAY** | Dispatch parallel-eligible tasks (empty or fully-satisfied <code>depends_on</code>) concurrently. Single-threaded dispatch is also valid. |
| **MAY** | Do its own internal retry on transient failures (network errors, rate limits). This is distinct from the plan-level retry semantics owned by <code>merge_results.py</code>. |
| **MAY** | Append a per-task entry to a cost ledger JSONL file (format: <code>{run_id, mpn, task_id, tier, model_id, tokens_in, tokens_out, cost_usd, success}</code>). Cost ledger location and format are documented per dispatcher. |
| **MUST NOT** | Modify <code>plan.json</code> itself. <code>merge_results.py</code> owns <code>execution.outcomes</code>. |
| **MUST NOT** | Skip schema validation. A result file with <code>status: "complete"</code> implies schema-valid by contract. |
| **MUST NOT** | Overwrite an existing <code>status: "complete"</code> result file without an explicit <code>--force</code> flag passed to the dispatcher. |
| **MUST NOT** | Edit or merge into <code><mpn>.json</code> directly. That's <code>merge_results.py</code>'s job. |
| **MUST NOT** | Read or generate <code><mpn>.json</code> (except for resume-safety idempotence checks against the result files, which is fine). |

## Retry semantics

The dispatcher is invoked **at most twice per pipeline run**:

1. **First invocation**: dispatch all <code>pending</code> tasks. Result files written. <code>merge_results.py</code> runs, validates, writes <code><mpn>.json</code>. If any task is <code>failed</code>, exit nonzero — the orchestrator that drives the pipeline re-invokes the dispatcher once more, re-running just the failed tasks (delete or rename their result files first to bypass idempotence).

2. **Second invocation** (<code>--retry-failed</code> mode in the dispatcher recipe): re-dispatch the failed tasks with the original prompt + appended error message ("Your previous output failed schema validation: <error>. Try again."). Write new result files (overwriting the failed ones). <code>merge_results.py --retry-failed</code> then runs; tasks that succeed merge cleanly; tasks that still fail get partial-merged with <code>{"_extraction_failed": true, "reason": ...}</code>.

The dispatcher does NOT loop on its own. The retry decision is owned by the orchestrator (or the user running the recipe).

## Failure classification

The dispatcher classifies subagent output as one of:

- **Hard failure**: malformed JSON, schema validation error, subagent returned an error message, missing required field. Set <code>status: "failed"</code>, populate <code>error</code>.
- **Soft / quality issue**: schema-valid but anomalous output (zero pins extracted, every absolute_max field null). Set <code>status: "complete"</code> and let the acceptance gate's quality score / sanity vector check catch the issue.

The dispatcher does NOT inspect the data semantically beyond schema validation. Quality checking is <code>datasheet_score.py</code>'s and <code>validate_sanity_vector.py</code>'s job.

## Cost ledger (optional, recommended for v1.4)

If the dispatcher writes a cost ledger, the format is JSONL:

<pre><code>
{"run_id": "20260425T100000Z-a1b2c3", "mpn": "LM2596-ADJ", "task_id": "scout", "tier": "B", "model_id": "claude-sonnet-4-6", "tokens_in": 12000, "tokens_out": 800, "cost_usd": 0.0123, "success": true, "extracted_at": "2026-04-25T10:00:30Z"}
</code></pre>

Location: <code><cache_dir>/_cost_ledger.jsonl</code>. Append-only; never rewritten.

The Phase 3a Claude Code recipe makes this optional (the cost data isn't auditable from inside the <code>Agent</code> tool); v1.5 SDK dispatchers populate it directly from API responses.

## Relationship to plan_extraction.py and merge_results.py

<pre><code>
                       plan_extraction.py
                               │
                               ▼
                       <mpn>.plan.json
                               │
                               ▼
            ┌──────────────────────────────────────┐
            │         DISPATCHER (this contract)   │
            └──────────────┬───────────────────────┘
                           │  result files
                           ▼
                       merge_results.py
                               │
                               ▼
                       <mpn>.json
</code></pre>

The dispatcher knows nothing about <code><mpn>.json</code>. Plan in, result files out.
