---
paths:
  - "tests/**"
  - "test-targets/**"
  - "scripts/Normalize-DapRecording.ps1"
---

# Testing

- `npm test` runs two ctest entries: the unit/integration tests, and the out-of-process `Replay`
  suite. Live integration + replay need `dbgeng.dll` and the built `test-targets` debuggees; they
  skip cleanly otherwise. A Release tree is exercised separately (`npm run configure:release` →
  `build:release` → `test:release`), and CI runs both Debug and Release.
- Tests resolve the adapter and debuggees relative to the test binary (see
  `tests/support/test_environment.*`), so both the Debug and Release trees run end-to-end.
- The test debuggees under `test-targets/testapp` always build unoptimized with full debug info
  (even in Release) so their line tables and locals stay stable for the recorded sessions and
  source-line assertions.

## Replay fixtures and the matcher

Setting a `trace` path in the launch/attach configuration records a session to a JSON file
(`{ "version": 1, "messages": [...] }`). `scripts/Normalize-DapRecording.ps1` turns a capture into
a portable replay fixture under `tests/replay/data`, which `tests/replay` replays out-of-process
against `dap-dbgeng.exe`.

The matcher requires every non-thread message (responses + all other events) to match in **strict
recorded order**, but the inherently nondeterministic `thread` lifecycle is loosened:

- `thread`-event **reordering** is tolerated (a couple of fixtures hit a benign dbgeng race where
  the terminating `thread` exit event lands just before/after the adjacent `stopped` event). Each
  fixture is retried twice.
- **Surplus** actual `thread` events are tolerated: how many threads a process and the loader/CRT
  spin up is environment-dependent, so a different OS build (e.g. a CI runner vs. the machine that
  recorded a fixture) can emit extra thread started/exited events. Every *recorded* thread event
  must still appear.
