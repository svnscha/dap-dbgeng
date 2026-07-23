---
name: dap-feature-recording-loop
description: Use when a new DAP feature needs replay coverage from real VS Code sessions, or when the user hands in a recording to verify, archive, and normalize into a fixture. Covers authoring the manual recording protocol (TODO.md) and the per-recording handoff cycle.
---

# DAP Feature Recording Loop

The co-working workflow for taking a DAP feature from implementation to
recorded-session replay coverage. The division of labor is fixed: the assistant
implements, verifies, and does all bookkeeping; the human performs the manual
VS Code sessions, because only a real client run proves the request ordering the
strict-order replay matcher asserts. Synthetic or hand-crafted fixtures are not
acceptable substitutes - a scripted driver once masked a bug that made a whole
feature unreachable from VS Code (setExpression, unreachable because the watch
evaluate path was broken and the driver never evaluated a watch).

## The loop

1. Implement the feature with unit/integration coverage, as usual.
2. Author the manual recording protocol (below) and leave it as a checklist the
   user can work off (repo-root `TODO.md`, untracked, plus the durable copy in
   `docs/development/recording-fixtures.md`).
3. The user records one scenario and hands back
   `recordings/launch.session.json` with words like "verify and archive".
4. Verify the recording (below). If it exposes a failure, switch to the
   `dap-analyze-trace` skill: fix the adapter first, have the user re-record;
   never normalize a failing trace into a fixture.
5. Archive + normalize + test (below), do the bookkeeping, commit, push.
6. Repeat until the checklist is done.

## Authoring the protocol (step 2)

Each scenario entry must name: the launch target (CMake Tools), the exact UI
gestures in order (which view, which button, which values), the expected stops
and values, and the fixture file it creates plus the `TEST(Replay, ...)` that
consumes it. Ground rules that go with every protocol:

- Start clean: no leftover breakpoints, watches, or exception filters.
- No hovering over variables while recording (each hover records an `evaluate`).
- End by continuing until the debuggee exits on its own.
- Copy `recordings/launch.session.json` to `recordings/<scenario>.session.json`
  immediately (every session overwrites it).
- The launch configuration must set `"dbgengPath"` so
  `Normalize-DapRecording.ps1` can tokenize it to `${dbgEngPath}`; without it
  the fixture only replays where the SDK Debugging Tools sit at the default
  path.
- Memory scenarios need Microsoft's Hex Editor extension in **Replace** mode
  (Insert-mode edits resize the file and fail to save with "Not supported").

## Verifying a handed-in recording (step 4)

Always format first; never reason from the raw JSON:

```powershell
pwsh scripts/Format-DapSessionFlow.ps1 -Path .\recordings\launch.session.json
```

Checklist against the scenario's expectations:

- Every request the scenario is about is present and `status=ok`; no
  unexpected `FAIL` lines.
- The stops (count, reason, values read back) match the scenario.
- The launch request carries `dbgengPath` (portability).
- The session ends with `exited` / `terminated` and a `disconnect` response.
- Spot-check the interesting payloads with ConvertFrom-Json (e.g. the dataId,
  the written bytes, the assigned value) - the flow view hides bodies.

A recording that fails its scenario is evidence, not a fixture: hand it to
`dap-analyze-trace`, fix the owning code path, and ask for a fresh recording.
Real client sessions have repeatedly exposed bugs synthetic tests missed (watch
expressions executed as engine commands; a stale bundled adapter shadowing the
build via the unexpanded `${workspaceFolder}` in `dap-dbgeng.adapterPath` -
check "Starting adapter: ..." in the dap-dbgeng output channel when live
behavior contradicts fresh code).

## Archive, normalize, test (step 5)

```powershell
Copy-Item recordings/launch.session.json recordings/<scenario>.session.json
pwsh scripts/Normalize-DapRecording.ps1 recordings/<scenario>.session.json tests/replay/data/<scenario>.json
./build/windows-x64/tests/dap-dbgeng-tests.exe --gtest_filter=Replay.<Name> --gtest_repeat=5
```

- Run with `--gtest_repeat` (5+); a pass that needed the harness's built-in
  retry is a flake to investigate, not a pass. Precedent: recorded thread-exit
  events are not reliably delivered at process teardown - that class belongs in
  the matcher's tolerance (see `.claude/rules/testing.md`), not in looser test
  assertions.
- Adjust the `TEST(Replay, ...)` assertions to the recorded shape when the real
  client's traffic differs from expectations (e.g. the hex editor probes
  unreadable pages, so successful-but-empty readMemory responses are normal);
  keep the assertion's intent, do not weaken it into counting nothing.
- Check for leaked `test_*` processes after the run.

## Bookkeeping (also step 5)

- `npm run matrix` and commit `docs/development/request-coverage.md` when it
  changes (fixtures feed the coverage page).
- Tick the scenario in `TODO.md`.
- Run the full test binary once before committing.
- Commit fixture + coverage page together; the message describes the recorded
  flow (what stops, what values). Push and confirm CI.
