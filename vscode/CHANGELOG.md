# Changelog

All notable changes to the Debug Adapter for WinDbg extension are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.1.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.0
