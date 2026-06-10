# Recording replay fixtures from VS Code

The replay tests under `tests/replay` are driven by recorded DAP sessions. The preferred source for
a fixture is a real VS Code session, because it captures the request flow an actual client
produces. This page lists the manual debugging sessions to perform and the exact gestures that
trigger each DAP request, so the captures can replace the synthesized fixtures.

## How a session gets recorded

Every configuration in `.vscode/launch.json` sets a `trace` path, so each debug session is
automatically written to `recordings/launch.session.json` (gitignored). Each new session
overwrites that file: **copy it to a scenario-specific name immediately after every session**, for
example:

```powershell
Copy-Item recordings/launch.session.json recordings/set-expression.session.json
```

Prerequisites:

- A built Debug tree: `npm run configure` and `npm run build` (adapter plus the `test-targets`
  debuggees).
- The "Debug Test Target (Launch)" configuration omits `program`, so select the debuggee per
  scenario via the CMake Tools active launch target in the status bar (`test_struct_1`,
  `test_launch`, `test_exception_1`, `test_data_1`).
- The memory scenario needs the Hex Editor extension (`ms-vscode.hexeditor`).

The deliverable per scenario is the raw, renamed capture under `recordings/`. Normalization
(`scripts/Normalize-DapRecording.ps1`), replacing the fixture under `tests/replay/data/`, and
adjusting the matching `TEST(Replay, ...)` assertions happen afterwards.

## Ground rules for clean captures

- One scenario per session, starting from a clean slate: before launching, remove all leftover
  breakpoints (source, function, instruction, and data), remove watch expressions, untick exception
  filters, and close the hex editor and disassembly view from a previous scenario. Anything left
  over injects extra requests into the capture.
- Avoid hovering over source or variables while recording (every hover is an extra `evaluate`
  request), and expand only the variables the scenario calls for. Extra traffic does not break the
  replay, but it bloats the fixture and adds nondeterminism.
- End every scenario by continuing until the program exits, and let the session end on its own.

## Scenarios

### 1. Memory read and write - replaces `modules-memory.json`

Target: `test_struct_1` (fixture asserts `p` at `{3, 7}`, then `42` written back).

1. Set a source breakpoint on `struct-1.cpp` line 37 (the `std::cout` line), then F5.
2. In the Variables view, select `p` and use **View Binary Data** (the inline button on the
   variable, provided by the Hex Editor extension). This sends `readMemory`.
3. In the hex editor, change the first byte from `03` to `2A` (42 little-endian) and save. This
   sends `writeMemory` followed by a re-read.
4. Close the hex editor, then continue to exit.

Note: the `modules` request in this fixture cannot be recorded from VS Code (see
[what cannot be recorded](#what-cannot-be-recorded-from-vs-code)); the recorded capture covers the
memory half, and the fixture keeps a synthesized or driver-recorded `modules` exchange.

### 2. Set expression - replaces `set-expression.json`

Target: `test_struct_1` (fixture assigns `t.origin.x = 123` and observes it).

1. Set a source breakpoint on `struct-1.cpp` line 37, then F5.
2. In the Watch view, add the expression `t.origin.x`.
3. Right-click the watch entry and choose **Set Value**, enter `123`. Editing a *watch* value sends
   `setExpression`; editing in the Variables view would send `setVariable` instead, so the Watch
   view is mandatory here.
4. In the Variables view, expand `t`, then `origin`, and confirm `x` reads `123`.
5. Remove the watch expression, then continue to exit.

### 3. Function breakpoints - replaces `function-breakpoints.json`

Target: `test_launch` (fixture stops in `main` via a deferred function breakpoint).

1. With no source breakpoints set, use the **+** button in the Breakpoints view header (**Add
   Function Breakpoint**) and enter `test_launch!main`.
2. F5: the session stops in `main`.
3. Continue to exit, then remove the function breakpoint.

### 4. Instruction breakpoints - replaces `instruction-breakpoints.json`

Target: `test_struct_1` (fixture stops at a disassembly address past the current one).

1. Set a source breakpoint on `struct-1.cpp` line 37, then F5.
2. When stopped, right-click in the editor and choose **Open Disassembly View** (this sends
   `disassemble`).
3. In the disassembly view, click the gutter of an instruction a few lines below the current one.
   This sends `setInstructionBreakpoints`.
4. Continue: the session stops at that instruction.
5. Remove the instruction breakpoint, then continue to exit.

### 5. Exception filter - replaces `exception-filter.json`

Target: `test_exception_1` (fixture stops first-chance on a caught `std::runtime_error`).

1. With no breakpoints set, tick the **C++ exceptions** checkbox in the Breakpoints view.
2. F5: the session stops on the `throw` with the exception widget open (the widget triggers
   `exceptionInfo`; expect exception code `0xE06D7363`).
3. Continue: the exception is caught locally, so the program runs to a clean exit.
4. Untick the **C++ exceptions** filter.

### 6. Data breakpoints - replaces `data-breakpoints.json`

Target: `test_data_1` (fixture arms a write watchpoint on `watched` and observes the write).

1. Set a source breakpoint on `data-1.cpp` line 15 (the "data-1 armed" line), then F5.
2. In the Variables view, right-click `watched` and choose **Break on Value Change**. This sends
   `dataBreakpointInfo` followed by `setDataBreakpoints`.
3. Continue: the watchpoint fires on the `watched = next * 2` write (line 17); `watched` now reads
   `4`.
4. **Remove the data breakpoint in the Breakpoints view before continuing.** The hardware
   watchpoint sits on a stack address that is reused after `main` returns, so a leftover watchpoint
   re-fires forever and the program never exits.
5. Continue to exit.

## What cannot be recorded from VS Code

- **`modules`** - VS Code has no Modules view (it is an open feature request,
  [microsoft/vscode#110067](https://github.com/microsoft/vscode/issues/110067)) and never sends the
  `modules` request, so no UI gesture can produce it. Keep this exchange synthesized via
  `scripts/Record-FeatureFixtures.mjs`.

## Turning a capture into a fixture

For each delivered `recordings/<scenario>.session.json`:

1. Normalize it into the fixture it replaces:

   ```powershell
   pwsh scripts/Normalize-DapRecording.ps1 recordings/set-expression.session.json `
       tests/replay/data/set-expression.json
   ```

2. Review the matching `TEST(Replay, ...)` in `tests/replay/replay_tests.cpp`: several tests assert
   values from the fixture (for example `watched` reading `4`, or exception code `0xE06D7363`) and
   their expectations may need adjusting to the recorded session's shape.
3. Run `npm test` (the `Replay` suite replays the fixture against `dap-dbgeng.exe`).
4. Run `npm run matrix` and commit `docs/development/request-coverage.md` if it changed (replay
   fixtures feed the coverage page).
