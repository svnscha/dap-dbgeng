# Changelog

All notable changes to the Native Windows Debugging (dbgeng) extension are documented
here. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-07-24

### Added

- One-key debugging for drivers and services, local or on a test machine: a `target`
  block in a launch configuration runs PowerShell command lines at the moments only
  the debugger knows - `beforeSession` (after the build task, before the adapter
  starts), `onAttachRequest`, `afterConfigurationDone` (breakpoints are armed), and
  `afterSessionEnd`. F5 can now deploy a driver to the target, start it so a
  `DriverEntry` breakpoint hits, and stop it again, with no manual step in between.
  Nothing is implicit: every command, its output, and its duration are logged to the
  dap-dbgeng output channel.
- General-purpose scripts for the usual steps ship with the extension:
  `Sign-Driver.ps1`, `Deploy-Binary.ps1` (replaces a registered service's binary in a
  single SSH round trip), `Start-RemoteService.ps1`, `Stop-RemoteService.ps1`, and
  `Ensure-ProcessServer.ps1`. Each takes `-HostName`, so the same command works
  against a machine over SSH or against this one with `localhost`. Use them from a
  hook via `${dbgengScripts}`, from anywhere else (`tasks.json` included) via
  `${command:dap-dbgeng.scriptsPath}`, or from a terminal - and replace any of them
  with your own script. None of them creates services: they replace the binary of one
  that is already registered, and say so when it is not.
- `Ensure-ProcessServer.ps1` also deploys the debugger to the target: when no
  `dbgsrv` is there, it copies the local Debugging Tools to `~\.dap-dbgeng\tools`
  (about 33 MB, once) and starts the process server from it, so remote user-mode
  debugging needs nothing installed on the target beyond SSH. Locally it is how a
  Windows service can be debugged without running the editor elevated: an elevated
  dbgsrv holds the rights the attach needs.
- Configuration snippets for kernel-driver and remote-service debugging in
  "Add Configuration...", plus the commands "Run Target Hook" and "Redeploy and
  Restart Target" (the latter also in the debug toolbar: stop, redeploy, and start
  again without detaching the debugger).
- Remote launch: `connectionString` on a launch configuration creates the process
  through a `dbgsrv` process server, so `program` and `cwd` are paths on the target
  and `stopAtEntry` works as it does locally.
- Attach by executable name: `processName` waits (up to `processNameTimeout`) for a
  matching process to appear, locally or on the `dbgsrv` host, which is what makes
  attaching to a service as it starts work.

### Fixed

- Kernel attach honors `stopAtEntry: false`: the target keeps running once the session
  is configured instead of always stopping, so driver debugging no longer needs a
  manual "continue" after every F5.
- Debugging through a `dbgsrv` process server failed with a bare
  `HRESULT=0x8007053D` (ERROR_SERVER_DISABLED). The engine loads `dbghelp.dll` by
  name at runtime and the loader hands it the older copy from System32, which the
  engine's process-server support does not work with; the adapter now loads the
  engine's own `dbghelp.dll` first so the pair matches. This also explains why the
  same engine worked when the debugger ran from the debugger's own directory.
- Pointing `dbgengPath` at the Store version of WinDbg and then debugging remotely
  now says what is wrong - it cannot load its own `dbghelp.dll`, so install the
  Debugging Tools for Windows - instead of failing with that HRESULT. Local and
  kernel debugging with it are unaffected.

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

[0.3.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.3.0
[0.2.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.2.0
[0.1.2]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.2
[0.1.1]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.1
[0.1.0]: https://github.com/svnscha/dap-dbgeng/releases/tag/v0.1.0
