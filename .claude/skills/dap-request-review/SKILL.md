---
name: dap-request-review
description: "Use when: reviewing or refactoring any DAP request handler, validating parse-first ordering, enforcing typed argument extraction over raw nlohmann::json, checking canonical option names with no alias fallbacks, or confirming that session/server state changes happen only after validated parsing."
---

# DAP Request Review

Use this skill when reviewing or tightening the implementation of any DAP request
handler such as `launch`, `attach`, `initialize`, `configurationDone`, `continue`,
`evaluate`, `stackTrace`, `setBreakpoints`, `pause`, `next`, `stepIn`, `stepOut`,
`threads`, `scopes`, `variables`, `terminate`, or `disconnect`.

This skill is for implementation quality, not coverage (see `dap-request-coverage` for
that). The goal is to make each handler easy to audit: parse first, validate required
inputs early, turn arguments into typed values, and only then touch debugger-session
state, server state, or protocol side effects.

## Principles

- Parse request arguments first. Do not interleave parsing with session creation,
  engine calls, thread selection, event emission, or state mutation.
- Validate required inputs before any side effect. If a required argument is missing or
  malformed, send the protocol error before creating or mutating anything.
- Prefer typed locals or a typed request-specific struct. Do not pass raw
  `nlohmann::json` into `dap_server` helpers, `debugger_session` methods, or
  engine-facing code - convert to typed values at the top and pass those down.
- Use canonical option names. The wire format and the implementation agree on one name
  per option (`program`, `cwd`, `dbgengPath`, `stopAtEntry`, `sources`, `symbolPath`,
  `processId`, `connectionString`, `kernel`, `dumpFile`). There are **no alias chains
  or legacy fallbacks** in this repo - keep it that way; do not add `a ?? b` reads.
- Use the project's `lower_case` naming for locals (a `stopAtEntry` argument reads into
  `stop_at_entry`); never carry a JSON spelling that differs from the canonical key.
- Keep comments rare but useful: explain why a branch, validation, or ordering
  constraint exists, not what the code does.
- Make the handler read top-to-bottom as: parse -> validate -> build typed inputs ->
  perform session/state changes -> send responses/events.

## Review Procedure

1. Identify the request boundary.

	Open the dedicated handler file, `src/service/dap_server_<command>.cpp`. This
	one-file-per-request layout is a project convention, so the boundary already exists;
	if a new request lacks its own file, create one before reviewing style or logic.

2. Inspect the top of the handler for parsing order.

	The first block should read arguments via `util::dap_argument_reader` (after
	`reader::get_arguments(current_request_json_)`) into typed locals or a small typed
	struct. Flag violations such as:

	- calling `create_debugger_session`, `require_debugger_session`,
	  `session.set_current_thread`, `session.launch`, `session.attach`,
	  `dispatcher_.invoke`, `send` / `send_response`, or other side effects before
	  required parsing is complete
	- mixing reader calls into later business logic instead of grouping them at the top
	- parsing the same property in multiple branches

3. Validate required inputs before side effects.

	For each required input, confirm the handler errors out (via `send_error_response`)
	before mutating any session or server state. Examples:

	- `program` must be validated before launch session creation
	- `processId` or `dumpFile` (or `kernel` + `connectionString`) must be validated
	  before attach session creation
	- `source.path` must be present before `set_source_breakpoints`
	- a positive `threadId` before an execution request (use the shared validators
	  `require_positive_thread_id` / `reject_single_thread` / `reject_target_id`)

4. Keep typed values below the parsing boundary.

	Once the handler has the raw arguments, convert them to typed values and pass those
	down. Review helper and session calls for leaked raw JSON.

	Bad patterns:

	- `apply_session_configuration(session, arguments)` (raw json)
	- a helper that takes `const nlohmann::json &arguments` when it needs a few typed
	  values
	- engine/session methods receiving reader outputs inline

	Preferred patterns:

	- `apply_session_configuration(session, configuration)` with a typed
	  `session_configuration{symbol_path, source_path}`
	- a request-specific struct (a `launch_options` / `attach_options`-style record)
	  when a request has several related options

5. Confirm there are no alias or fallback reads.

	The canonical key is read once. There must be no `try_get_string(args, "program")`
	falling back to an old name, and no compatibility spellings. If you find one, remove
	it (this repo deliberately carries no legacy aliases).

6. Check session/server state mutation ordering.

	After parsing and validation, verify side effects are ordered coherently:

	- create or require the session only after validation passes
	- apply typed configuration before the action that depends on it
	- mutate `launch_awaiting_configuration_done_`, `launch_stop_at_entry_`,
	  `launch_thread_id_`, `detach_on_disconnect_`, `terminate_debuggee_on_disconnect_`,
	  `is_execution_running_`, or `pending_stopped_event_` only after the underlying
	  action has succeeded far enough to justify that state
	- send the response and events only after committing to the successful path
	- engine calls run on the dispatcher (`dispatcher_.invoke`); only
	  `IDebugControl::SetInterrupt` (via `session.interrupt()`) is called off-dispatcher

7. Check comments for value.

	Keep comments that explain a protocol or ordering constraint (why validation
	precedes session creation, why a state flag flips only after an operation, why an
	interrupt-then-resume is used). Remove comments that merely narrate code.

8. Finish with a verdict.

	Classify findings:

	- Parsing-order issue: parsing and side effects are interleaved
	- Validation issue: a required input is not rejected early enough
	- Alias/fallback issue: a legacy name or `a ?? b` read leaked in
	- Typing issue: raw `nlohmann::json` passed below the handler boundary
	- Readability issue: no named helper for a repeated parsing rule
	- Commentary issue: missing or low-value comments in non-obvious logic

	When the fix is clear, name a concrete refactor target (for example "extract
	`read_launch_options(arguments)`") rather than only describing the smell.

## Completion Checklist

Before calling a handler review complete, confirm:

- the handler lives in its own `dap_server_<command>.cpp`
- parsing is grouped at the top of the handler
- required inputs are validated before side effects
- there are no alias chains or legacy-name fallbacks
- helpers and session methods receive typed values, not raw `nlohmann::json`
- locals use the project's `lower_case` naming for option values
- session and server state are mutated only after validated parsing
- comments, if present, explain why rather than what
- the file builds warning-clean at `/W4 /WX` and is clang-format clean

## Reporting Template

```text
Focused request: <request>
Handler file: src/service/dap_server_<command>.cpp
Verdict: <clean / needs refactor / blocked>

Findings:
- <severity>: <issue> -> <why it matters> -> <recommended refactor>

Approved patterns:
- <typed parsing or ordering choice that should be kept>

Refactor direction:
1. <parse/normalize extraction>
2. <validation reorder>
3. <typed helper boundary>

Completion check:
- <which checklist items pass>
- <which still fail>
```

## Example Prompts

- Review the `launch` handler with `dap-request-review`.
- Use `dap-request-review` on `attach` and flag any raw `nlohmann::json` passed to
  session helpers.
- Apply `dap-request-review` to `setBreakpoints` and check its validation ordering.
- Use `dap-request-review` across all `dap_server_*.cpp` handlers and list parse-first
  violations.
