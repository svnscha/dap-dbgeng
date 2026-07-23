# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-07-23

### Added

- Ten new DAP requests, each end-to-end tested with live integration tests and
  replay fixtures recorded from real VS Code sessions (the recording protocol
  lives in `docs/development/recording-fixtures.md`):
  - `modules` lists the loaded modules with image path, address range, and
    symbol status.
  - `readMemory` / `writeMemory` give clients (e.g. VS Code's hex editor) raw
    access to debuggee virtual memory; locals now carry a `memoryReference`.
  - `setExpression` assigns any in-scope l-value expression (e.g. `t.origin.x`)
    through the engine's native symbol-group write.
  - `setFunctionBreakpoints` sets name-based breakpoints (`bu`), deferred until
    the module loads.
  - `setInstructionBreakpoints` sets address breakpoints from the disassembly
    view.
  - `setExceptionBreakpoints` with one filter: break on first-chance C++
    exceptions (`sxe e06d7363`).
  - `exceptionInfo` describes the current exception stop (code, address, first
    or second chance).
  - `dataBreakpointInfo` / `setDataBreakpoints` arm hardware write/read-write
    watchpoints (`ba`) on locals and struct fields.
- New test debuggees `exception-1.cpp` (caught C++ throw) and `data-1.cpp`
  (watched write).
- Expand structs in the Locals view. Locals backed by aggregates (structs, classes,
  nested members) now report a non-zero `variablesReference` and expand to their
  members, read from the dbgeng scope symbol group via `IDebugSymbolGroup2` with
  bounded (depth- and node-limited) eager expansion. Each member carries its type
  name and a full `evaluateName` access path; pointers are not auto-followed.
- Set nested struct fields through `setVariable`. A field is assigned by its
  in-scope expression (for example `t.origin.x`) using the engine's own symbol-group
  write (`AddSymbol` + `WriteSymbol`), so the value is parsed and stored with type
  awareness. Top-level locals and registers are unchanged.
- A `struct-1.cpp` test debuggee (point2 / vector3 / nested transform) plus live
  integration tests and recorded replay fixtures (`struct-locals.json`,
  `struct-setVariable.json`) covering expansion and field assignment.

### Fixed

- Watch entries are evaluated as C++ expressions (frame-scoped, e.g.
  `t.origin.x`) instead of being executed as engine commands, which failed for
  any plain expression and made `setExpression` unreachable from the Watch
  pane. Input that does not resolve as an expression still runs as a native
  command.
- Updating function, instruction, data, or exception breakpoints while the
  debuggee runs no longer blocks the adapter behind the engine's wait loop;
  user-mode targets briefly interrupt and resume (like source breakpoints),
  kernel targets report "pause first".
- The first-chance C++ exception filter is honored when disabled: a locally
  caught throw no longer stops the session, and `exceptionInfo` no longer
  describes a stale exception from an earlier stop.
- Detach clears all breakpoints and zeroes the per-thread debug registers
  before letting the target go; a leftover hardware watchpoint used to freeze
  the detached process on its next hit.
- `writeMemory` rejects malformed base64 instead of writing a truncated
  payload, and `readMemory`/`writeMemory`/`setInstructionBreakpoints` reject
  offsets that wrap the 64-bit address space and negative memory references.
- The VS Code extension expands `${workspaceFolder}` in the
  `dap-dbgeng.adapterPath` setting and logs when it falls back to the bundled
  adapter; the configured path was previously ignored silently.
- `scripts/Normalize-DapRecording.ps1` now tokenizes a Windows `dbgengPath`
  (backslash form) to `${dbgEngPath}`, matching how the repository root is handled.

## [0.1.2] - 2026-06-08

Initial release. A Windows Debug Adapter Protocol server backed by the Windows
debug engine (`dbgeng` / `dbghelp`), letting DAP clients such as VS Code drive
native Windows debugging: launch and attach (including remote via `dbgsrv` and
kernel), breakpoints, stepping, stack traces, scopes and variables, registers,
disassembly, memory, and expression evaluation.

[Unreleased]: https://github.com/svnscha/dap-dbgeng/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/svnscha/dap-dbgeng/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.2
