---
name: dap-analyze-trace
description: Use when analyzing a recorded dap-dbgeng session trace or investigating a live debugging bug from its DAP request/response/event flow.
---

# DAP Analyze Trace

Use this workflow when the user provides a recorded session trace, describes a live
debugging failure, and wants the investigation driven by the real request and event
flow before deciding whether code changes are needed.

A trace is the JSON file written when a launch/attach configuration sets a `trace`
path (for example `"trace": "${workspaceFolder}/recordings/session.json"`). It has the
`{ "version", "messages": [ { direction, message } ] }` shape - the same format as the
replay fixtures under `tests/replay/data`.

This skill is intentionally human-in-the-loop. The first pass is for trace formatting,
protocol analysis, and a focused code fix or clarification request. Do not create a new
replay fixture or automated regression test from the first failing trace. First fix the
implementation based on the trace plus the user's description, then ask the user to
rerun the live scenario. Only if that rerun succeeds should the fresh trace become an
automated replay back-test.

## Principles

- Start from the user's reported live behavior, not from a guessed test scenario.
- Format the trace first so request, response, and event ordering are easy to inspect
  before reasoning about code.
- Treat the trace as evidence of protocol ordering, timing, and state transitions.
- Do not claim the bug is fixed because a synthetic or partial test passes while the
  live scenario still fails.
- Keep the first implementation change focused on the behavior the user described and
  the trace supports.
- Ask for missing reproduction details if the trace and description are insufficient to
  identify the owning code path.
- Do not create a new recorded-session fixture or replay test from the initial failing
  trace.
- After making a fix, ask the user to rerun the real scenario and provide a fresh trace.
- Only after the user confirms the live scenario behaves correctly should the fresh
  trace become the source for automated back-testing.

## Required First Step

Before analyzing code, format the trace with the repository script:

```powershell
pwsh scripts/Format-DapSessionFlow.ps1 -Path .\recordings\session.json
```

Use `-IncludeOutput` only when the engine's console output is needed to explain the
behavior. Prefer the compact flow first. If the formatter is missing or broken, fix
that tooling first - readable flow inspection is part of this workflow.

## Architecture context

This adapter's behavior is shaped by dbgeng thread-affinity, so ordering and teardown
bugs usually live at thread boundaries. Keep these in mind when reading a trace (see
`.claude/rules/threading.md`):

- The DAP transport runs on the main thread; all engine work runs on one dispatcher
  thread (`util::debugger_session_dispatcher`).
- Outbound responses/events go through a single FIFO writer, so wire order is
  deterministic - if the trace shows a bad order, the bug is in *when* something was
  queued, not in transport racing.
- A wait-for-session-event loop pumps `WaitForEvent`; engine callbacks
  (`wire_debugger_session` in `src/service/dap_server.cpp`: `on_breakpoint_hit`,
  `on_break_hit`, `on_exception_hit`) set a pending-stopped state and emit
  `stopped` / `thread` / `exited` / `terminated`.

## Workflow

1. Confirm the investigation inputs.

	Identify the trace file path, the expected behavior, the actual observed behavior,
	and whether the issue happened in a live run or only in a test. If any are missing,
	ask before drawing conclusions.

2. Format and summarize the flow.

	Run the formatter and reduce the relevant portion to a short timeline: inbound
	requests, outbound responses, and important events (`stopped`, `continued`,
	`thread`, `exited`, `terminated`), plus whether the failure happened before, during,
	or after teardown. Do not edit code until you can state one falsifiable hypothesis
	from that flow.

3. Identify the owning control path.

	Start from the request handler or event-queueing path that directly controls the
	broken behavior. Prefer the local implementation surface over broad exploration:

	- a failing `threads` response -> `src/service/dap_server_threads.cpp` and
	  `get_threads` / `try_get_current_thread_id`
	- wrong `stopped` / `continued` ordering -> the session-event coordinator
	  `src/service/dap_server_session_events.cpp`, the launch/configuration flow
	  (`dap_server_launch.cpp`, `dap_server_configuration_done.cpp`,
	  `continue_after_configuration_done`), and the engine callbacks in
	  `dap_server.cpp` (`wire_debugger_session`, `set_pending_stopped_event`)
	- bad request validation -> the dedicated `src/service/dap_server_<command>.cpp`
	- engine-call behavior -> `src/debugger/debugger_session_*.cpp`

4. Decide whether the trace is sufficient.

	If the trace clearly identifies the faulty behavior, continue into a focused fix.
	Otherwise ask for the smallest missing detail: launch settings used, whether
	breakpoints were set, whether `stopAtEntry` was enabled, whether the target exited
	normally or crashed, or a fresh trace if the current one is stale.

5. Make the smallest credible fix.

	Patch the owning code path that explains the trace. Keep it local and
	behavior-oriented: correct teardown-state handling, fix request validation or guard
	conditions, move logic to the right request/event boundary, or preserve protocol
	correctness under a race the trace shows. Avoid papering over the bug with a broad
	`catch (...)`, adding a regression fixture from the failing trace, or declaring
	success without a live rerun.

6. Run focused validation, subordinate to the live retest.

	After the first substantive edit, run the narrowest validation available:

	```powershell
	npm run build
	./build/windows-x64/tests/dap-dbgeng-tests.exe --gtest_filter=<Suite.Case>
	```

	or `npm run test:replay` for a recorded-session check. Use this to catch obvious
	regressions, not as proof the live bug is solved.

7. Hand control back for the live retest.

	If the change is plausible and focused validation passes, ask the user to rerun the
	real scenario and capture a fresh trace. Explicitly ask whether the original failure
	is gone, for the new trace file, and about any remaining unexpected behavior.

8. Only then create automated back-test coverage.

	Once the user confirms the live scenario behaves correctly and provides the fresh
	trace:

	- normalize it into a portable fixture:
	  `pwsh scripts/Normalize-DapRecording.ps1` -> `tests/replay/data/<name>.json`
	- add a `Replay.<Name>` test in `tests/replay/replay_tests.cpp`
	- run `npm run test:replay`

	Do not back-test against the original failing trace unless the explicit goal is to
	preserve a failure-mode contract.

## Decision Rules

- If the formatter output already disproves the user's hypothesis, say so and explain
  the actual ordering the trace shows.
- If the trace points to a race, fix it at the behavior boundary (the dispatcher /
  event callback / pending-stopped gate) instead of only asserting one ordering in a
  test. Note that the replay matcher already tolerates `thread`-event reordering and
  surplus thread events (see `.claude/rules/testing.md`), so a real ordering bug must be
  visible in the non-thread message order.
- If a test passes but the user's live run still fails, trust the live evidence.
- If the fix needs broad refactoring, split it: a minimal behavioral repair first, a
  cleanup follow-up second.
- If the trace is ambiguous, prefer one nearby disambiguating read or one targeted
  question over broad exploration.

## Explicit Non-Goal For First Pass

On the first pass, do not: create a new replay fixture from the failing trace, add a
regression test just because a trace exists, or mark the issue resolved based only on a
new test or local replay. Those belong only after the user reruns the live scenario and
the fresh trace confirms the intended behavior.

## Reporting Template

```text
Trace file: <path>
User expectation: <expected behavior>
Observed behavior: <actual behavior>
Flow summary:
- <timeline point>
- <timeline point>

Local hypothesis:
- <one falsifiable explanation tied to the formatted flow>

Owning code path:
- <handler, helper, or event path>

Action taken:
- <code fix, validation, or clarification request>

Status:
- <needs user retest / blocked on details / live fix ready for verification>

Next user step:
1. <rerun scenario>
2. <attach fresh trace>
3. <confirm whether the live failure is gone>
```

## Example Prompts

- Use `dap-analyze-trace` on `recordings/session.json`; the user says `threads` fails
  when the target exits immediately.
- Apply `dap-analyze-trace` to this launch trace and determine whether
  `configurationDone` or teardown handling is wrong.
- Use `dap-analyze-trace` for a live `terminated` ordering issue; fix the code first,
  then ask for a fresh trace before adding any replay coverage.
