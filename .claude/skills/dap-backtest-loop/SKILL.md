---
name: dap-backtest-loop
description: Use when back-testing dap-dbgeng features against real targets with scripted VS Code sessions - driving scenarios from dap-dbgeng-rc, recording DAP traces, reading them for bugs, and turning the good ones into replay fixtures. Covers what can run in parallel and what cannot.
---

# Back-testing features against real targets

The loop is: **script a scenario -> run it -> read the trace -> fix what it
exposes -> keep the trace as a fixture**. A feature is back-tested when a
recorded session proves it works against a real target and a replay test locks
that behavior in.

The harness is `C:\Users\svnscha\Code\dap-dbgeng-rc` (`uv run dap-dbgeng-rc run
<scenario>`). Scenarios live in `src/dapdbgeng_rc/scenarios/`.

## What can and cannot run in parallel

Live scenarios **cannot** run in parallel, and no amount of fanning out changes
it:

- one desktop: two windows fight over focus, and the Remote Control extension
  binds one port,
- one kernel target: KDNET accepts a single debugger,
- one service per machine: starting it twice is the same service.

Run live scenarios one at a time. What *does* parallelize is everything after
the trace exists: reading traces, cross-checking the request matrix, drafting
fixtures and tests. Fan out there.

## Running a scenario

The launch configuration must set `trace` (a path under `recordings/`), or the
run produces nothing to analyze:

```jsonc
"trace": "${workspaceFolder}/recordings/kernel.session.json"
```

The trace is written when the **adapter exits cleanly**, from the recorder's
destructor. A killed adapter leaves no trace - which is itself a finding, not a
harness glitch. Copy the file to a scenario-specific name straight after the
run; the next run overwrites it.

## Reading the trace, not the screen

A scenario that sleeps through a failed session looks exactly like one that
worked: steps print, the window looks plausible, nothing fails. **Judge a run by
its trace**, never by the step log.

```powershell
pwsh scripts/Format-DapSessionFlow.ps1 -Path recordings/<scenario>.session.json
```

Check: every request the scenario is about is present and `status=ok`; the stops
match what the scenario asked for; the session ends with `exited`/`terminated`
and a `disconnect` response. Screenshots are for what the *viewer* sees; the
trace is for what the *adapter* did. Both lie in different directions, so use
each for its own question.

## Turning a good run into a fixture

Only after the trace shows what the scenario claims:

```powershell
pwsh scripts/Normalize-DapRecording.ps1 recordings/<s>.session.json tests/replay/data/<s>.json
./build/windows-x64/tests/dap-dbgeng-tests.exe --gtest_filter=Replay.<Name> --gtest_repeat=5
npm run matrix   # the coverage page is generated from fixtures
```

A failing trace is evidence, not a fixture. Fix the adapter, re-record.

## Bugs this has already found

Keep these in mind - the same shapes recur:

- **Kernel attach can hang forever.** `attach_kernel` ends in
  `WaitForEvent(INFINITE)`; a target that never answers hangs the attach
  request and holds the dispatcher thread, with no feedback and no way to stop
  the session. It cannot be fixed with a timeout argument: kernel
  `WaitForEvent` rejects a finite timeout with `E_NOTIMPL` (0x80004001). A fix
  has to unblock the wait another way - a watchdog calling `SetInterrupt`
  (documented as the one cross-thread-safe call) or ending the session.
- **Handler errors reached the user as "Failed to handle command: X".** The
  catch-all in `dap_server.cpp` discarded the reason; it now unwraps through
  `debugger_session_dispatcher::unwrap_failure_message`, as the individual
  handlers already did. Any new catch site should do the same.

## Coverage gaps to work through

Checked against `docs/development/request-coverage.md`:

- **no kernel fixture exists at all** - the headline feature has zero replay
  coverage,
- `pause`, `modules`, `source` are implemented and never exercised,
- nothing covers `processName` attach or launch through a process server.
