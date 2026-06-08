---
name: dap-request-coverage
description: Use when validating or improving test coverage for a DAP request handler, using the session replay tests and request matrix as the source of truth.
---

# DAP Request Coverage

Use this workflow to validate, explain, or improve coverage for a specific DAP request
area such as `launch`, `initialize`, `attach`, `configurationDone`, `stackTrace`,
`scopes`, `variables`, `continue`, `evaluate`, `setBreakpoints`, or `disconnect`.

Each implemented request already lives in its own translation unit
(`src/service/dap_server_<command>.cpp`) - that handler-file boundary is a project
convention, so unlike the prior C# layout you do not need to extract a partial first;
just confirm the file exists and is the inspection target.

The most important coverage signal in this repository is command verification through
the out-of-process **session replay tests** (`tests/replay`). Treat replay coverage as
the broader source of truth: it validates request/response ordering against realistic
DAP traffic recorded from real sessions. Line/branch coverage tooling (covdbg) is a
later addition; until then, the replay fixtures plus the request matrix are the
coverage map.

## Principles

- Start from behavior: identify the DAP command, its expected protocol messages, and
  the existing replay fixtures before chasing percentages.
- Prefer session replay verification for broad command behavior and message ordering.
- Do not add direct `dap_server` request-behavior assertions where a replay fixture is
  the right surface. Verify request behavior through recorded session replay.
- If a request's behavior is not covered, recommend capturing or updating a recorded
  session fixture that replays the scenario - do not hard-code request behavior into a
  unit test instead.
- Reserve focused unit tests (`tests/util/*`, `tests/service/dap_server_handler_tests.cpp`,
  `tests/service/dap_server_tests.cpp`) for pure logic that sits *below* the request
  surface: argument parsing, classifiers, the engine-path resolver, pure helpers.
- Use coverage to find meaningful missing behavior, not to chase a number.

## Workflow

1. Identify the request area.

	Determine the DAP command and its handler (`launch` -> `handle_launch_request` in
	`src/service/dap_server_launch.cpp`). Search for the command string, the response
	type, and existing replay fixtures.

2. Map current coverage with the request matrix.

	The matrix lives as a generated, committed documentation page,
	`docs/development/request-coverage.md`, listing per command whether it is
	implemented and which replay fixtures exercise it. Read that page first.

	```powershell
	npm run matrix          # regenerate docs/development/request-coverage.md
	npm run matrix:check    # verify the committed page is up to date (CI-enforced)
	pwsh scripts/Get-DapRequestMatrix.ps1 -Stdout   # quick table view, no file write
	```

	The generator reads the generated dispatch (`src/protocol/dap_service.h`), the
	handler files, and the fixtures under `tests/replay/data`, so it tracks the real
	surface without a hand-maintained list. After you add a handler or a fixture,
	regenerate the page and commit it (CI fails via `npm run matrix:check` if it is
	stale).

3. Read the relevant fixtures and assertions.

	For the target command, inspect the recorded messages in the fixtures that include
	it and the matching `Replay.<Name>` assertions in `tests/replay/replay_tests.cpp`,
	so you know which protocol behavior is already covered.

4. Run the replay suite.

	```powershell
	npm run test:replay   # ctest --preset windows-debug -R Replay
	```

	or the whole suite with `npm test`. Replay needs `dbgeng.dll` and the built
	`test-targets` debuggees; it skips cleanly otherwise, so a green run with skips
	means the native prerequisites were absent (note that, do not call it covered).

5. Classify each uncovered area into one bucket.

	- Replay gap: realistic DAP traffic should cover this command behavior -> add or
	  update a recorded session (see `dap-analyze-trace` for capturing one).
	- Helper gap: the missing logic is pure parsing/classification/resolution below the
	  request surface -> add a focused unit test.
	- Transport / entry-point gap: not reached by replay because the driver bypasses
	  stdio or `main()`; do not call it dead code without a usage search.
	- Possible dead code: no production reference or replay/test path reaches it; verify
	  with a workspace search before recommending deletion. (This repo keeps no
	  compatibility shims, so genuinely unreachable code should be removed, not tested.)

6. Improve coverage in the right order.

	First improve replay coverage for command-level behavior and message ordering. Then
	add helper tests for pure logic beneath the request surface. If request behavior is
	still missing, recommend a new recorded session rather than encoding it in a unit
	test.

7. Verify after each meaningful step.

	After updating replay data, run `npm run test:replay`. After adding unit tests, run
	the focused binary:

	```powershell
	./build/windows-x64/tests/dap-dbgeng-tests.exe --gtest_filter=<Suite.Case>
	```

	Keep our targets warning-clean (`/W4 /WX`); a new warning fails the build.

## Request Coverage Checklist

For each request handler, check:

- happy-path response type and `success` state
- required protocol events and their order
- required argument validation (the early error before any side effect)
- string, boolean, numeric, array, and malformed JSON argument shapes where applicable
- unsupported-mode branches where applicable (for example singleThread / targetId on
  the execution requests)
- state transitions that influence later requests
- cleanup behavior on `disconnect`, `terminate`, or session end
- a replay fixture covering the realistic command flow
- helper tests for pure parsing/classification that replay should not encode

## Request Focus Examples

Focus areas for `launch`:

- `noDebug == true` error response
- missing or blank `program`
- `dbgengPath` selection and auto-resolution (explicit path -> bundled -> Windows SDK)
- `cwd` handling (defaults to the program's directory when omitted)
- `args` as a string, as an array, empty array, and malformed values
- `stopAtEntry` true vs false: false runs through configurationDone with no entry stop;
  true surfaces the entry stop and stays halted
- process and stopped event ordering
- `program` defaulting to the CMake Tools launch target (resolved in the extension, so
  the adapter receives a concrete path - covered by the live/extension layer, not replay)
- disconnect cleanup after a launched process

Focus areas for `initialize`:

- the advertised `Capabilities` set (the source of truth is
  `dap_server_initialize.cpp`)
- `InitializedEvent` emission and order
- replay compatibility for clients that send initialize before launch
- the once-only guard (a second initialize is an error)

## Reporting Template

```text
Focused command: <command>
Handler file: src/service/dap_server_<command>.cpp
Matrix: <implemented? which fixtures exercise it>
Verification command: <npm run test:replay / focused gtest>
Result: <passed/failed/skipped totals>

Session replay coverage:
- <what realistic behavior is verified by which fixture>

Gaps:
- <behavior> -> <replay gap / helper gap / transport gap / dead code> -> <recommended action>

Dead-code assessment:
- <code path> -> <why dead, unreachable, or just uncovered by this test type>

Next steps:
1. <highest-value replay or fixture step>
2. <targeted helper unit test>
3. <follow-up verification>
```

Keep the report grounded in the matrix output and the actual test results. If replay
skipped because prerequisites were missing, say so before drawing conclusions.

## Example Prompts

- Use `dap-request-coverage` on `launch` and list which fixtures exercise it.
- Apply `dap-request-coverage` to `setBreakpoints`; is the running-target path covered?
- Use `dap-request-coverage` to find DAP commands with no replay fixture via the matrix.
