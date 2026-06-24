---
name: perf-review
description: >-
  Review code changes for performance regressions and optimization opportunities.
  Use when the user asks for perf-review, performance review, slow paths, memory
  leaks, N+1 queries, hot-loop regressions, or latency issues.
disable-model-invocation: true
---
# Perf Review

Use this skill when the user asks to run `/perf-review` or requests a performance review.

Follow the review checklist in `.cursor/rules/performance-reviewer.mdc`. For this C++ game-server codebase, also prioritize:

- Handler hot paths: blocking IO, long locks, or cross-thread writes in `Poll()` handlers
- Per-tick or per-message allocations and copies on the network path
- Unbounded containers (`unordered_map`, queues) without eviction or caps
- MySQL query patterns in Login/Record/Session (N+1, missing indexes, unbounded `SELECT`)
- AOI/Scene broadcast fan-out and redundant serialization
- Lua/C++ boundary call frequency and table churn in SceneServer

## Workflow

1. Determine scope: default to branch changes (`git diff` against merge-base); use uncommitted only if the user asks.
2. List changed files with `git diff --name-only` (or the user's PR/branch target).
3. Launch exactly one read-only `explore` subagent with `subagent_type: "explore"` and `readonly: true`.
4. In the subagent prompt, include the changed file list and instruct a **PERFORMANCE-only** review using the categories in `performance-reviewer.mdc`, plus the server-specific bullets above.

Subagent prompt shape:

```text
Read-only review: PERFORMANCE

Repository: <absolute path>
Changed files:
<file list>

Focus: hot-path regressions, algorithmic complexity, memory lifetime/leaks,
unbounded growth, sync IO on event loop, DB N+1, redundant copies/serialization,
missing indexes, sequential work that could be batched.

Output:
- Critical / Warning / Info findings
- file:line, category, issue, concrete fix
- "No issues" if nothing material in the diff
```

5. After the subagent finishes, summarize:
   - Empty diff → one sentence, no findings.
   - No issues → one-line status.
   - Otherwise → markdown table: Severity | Location (file:line) | Finding | Suggested fix (brief).

Do not fix findings unless the user explicitly asks.
