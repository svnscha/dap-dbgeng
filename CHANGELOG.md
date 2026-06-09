# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

- `scripts/Normalize-DapRecording.ps1` now tokenizes a Windows `dbgengPath`
  (backslash form) to `${dbgEngPath}`, matching how the repository root is handled.

## [0.1.2] - 2026-06-08

Initial release. A Windows Debug Adapter Protocol server backed by the Windows
debug engine (`dbgeng` / `dbghelp`), letting DAP clients such as VS Code drive
native Windows debugging: launch and attach (including remote via `dbgsrv` and
kernel), breakpoints, stepping, stack traces, scopes and variables, registers,
disassembly, memory, and expression evaluation.

[Unreleased]: https://github.com/svnscha/dap-dbgeng/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.2
