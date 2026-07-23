# Changelog

All notable changes to the Native Windows Debugging (dbgeng) extension are documented
here. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-07-23

### Added

- Raw memory access: locals carry a memory reference, so a variable's memory can be
  inspected and edited in a binary/hex editor (right-click a variable, "View Binary
  Data").
- Data breakpoints: break when a local or a struct field is written (or read), backed
  by hardware watchpoints (right-click a variable, "Break on Value Change").
- Function breakpoints: break on a function by name from the Breakpoints pane;
  deferred until the containing module loads.
- Instruction breakpoints: set breakpoints on individual instructions in the
  Disassembly view.
- First-chance C++ exceptions: opt in from the Breakpoints pane to stop where an
  exception is thrown, with the exception's code, address, and chance reported on the
  stop.
- Assignment from the Watch pane: any in-scope l-value expression (for example
  `t.origin.x`) can be assigned a new value.
- Structs, classes, and nested members expand in the Variables view, and struct fields
  can be edited in place.
- Loaded modules are reported with image path, address range, and symbol status for
  DAP clients that surface them.

### Fixed

- Watch entries are evaluated as C++ expressions instead of being run as engine
  commands; input that does not resolve as an expression still runs as a native command.
- Changing breakpoints while the target is running takes effect without blocking the
  debug session.
- Detach removes all breakpoints, including hardware watchpoints, before letting the
  target go; a leftover watchpoint used to freeze the detached process.
- The `dap-dbgeng.adapterPath` setting expands `${workspaceFolder}` and logs when the
  extension falls back to the bundled adapter; a configured path was previously ignored
  silently.

## [0.1.2] - 2026-06-08

### Changed

- Redesigned the extension icon: a Windows four-pane window with the top-left pane as a
  red breakpoint dot, in the brand blue.
- Reworked the Marketplace listing to be user-facing - it follows the getting-started
  guide, shows the most common `launch.json` configurations, and embeds the promo and
  scenario demo videos.

No functional changes.

## [0.1.1] - 2026-06-07

### Changed

- Renamed the extension to **Native Windows Debugging (dbgeng)** (from "Debug Adapter for
  WinDbg"), which better describes what it does: debug native Windows code (C and C++
  programs, services, and the Windows kernel) from VS Code using the same engine that
  powers WinDbg. No functional changes.

## [0.1.0] - 2026-06-07

Initial release.

### Added

- Bundled `dap-dbgeng` debug adapter (a single static `dap-dbgeng.exe`); no separate
  download or build required.
- `dbgeng` debug type with `launch` and `attach` requests.
- Launch debugging with `program`, `args`, `cwd`, `stopAtEntry`, `sources`, `symbolPath`,
  and `dbgengPath`. With the CMake Tools extension installed, `program` is optional and
  resolves to the active launch target.
- Attach debugging: local process, remote process via a `dbgsrv` `connectionString`,
  crash dump (`dumpFile`), and kernel-mode targets (`kernel: true`).
- Line and conditional breakpoints; step over, into, and out, including instruction-level
  stepping in the Disassembly view.
- Call stack with on-demand frame loading; variables, scopes, and registers; set variable;
  disassembly view; and expression evaluation in the Watch pane and Debug Console.
- `dap-dbgeng.pickProcess` command for interactive process selection
  (`"processId": "${command:dap-dbgeng.pickProcess}"`), including processes on a `dbgsrv`
  host.
- `dap-dbgeng.adapterPath` setting to override the bundled adapter.

[0.2.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.2.0
[0.1.2]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.2
[0.1.1]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.1
[0.1.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.0
