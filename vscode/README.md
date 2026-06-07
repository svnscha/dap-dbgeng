# Debug Adapter for WinDbg

Native Windows debugging in VS Code, powered by the Windows debug engine (`dbgeng`).

Debug native user-mode programs and Windows kernel targets through the Debug Adapter
Protocol: launch or attach, set breakpoints, step, inspect the call stack, locals,
registers, and disassembly, and evaluate expressions through the engine, with no fragile
text scraping of WinDbg output.

[Documentation and guides](https://svnscha.github.io/dap-dbgeng/) | [Getting started](https://svnscha.github.io/dap-dbgeng/getting-started/)

<!-- A short demo GIF goes well here once one is recorded. -->

## Requirements

- 64-bit Windows.
- **Debugging Tools for Windows**, which provides `dbgeng.dll` (usually
  `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll`). Install it through
  the Windows SDK or WDK installer (tick *Debugging Tools for Windows*). The adapter finds
  `dbgeng.dll` automatically from the installed SDK; set `dbgengPath` only to pin a
  specific copy.

The debug adapter itself is **bundled in the extension**, so there is nothing else to
build, download, or point at.

## What it does

- **Launch, attach, remote (`dbgsrv`), and kernel** debugging through one adapter.
- **Line and conditional breakpoints.**
- **Step over / into / out**, including instruction-level stepping in the Disassembly view.
- **Call stack** with on-demand frame loading.
- **Variables, scopes, and registers**, plus **set variable** from the Variables pane.
- **Expression evaluation** in the Watch pane and Debug Console, through the engine.
- **Disassembly view.**

See [Supported features](https://svnscha.github.io/dap-dbgeng/reference/features/) for the
full, honest list of what is and is not supported.

## Quick start

1. Install this extension on a 64-bit Windows machine and open your C++ workspace.
2. Confirm `dbgeng.dll` is present:

   ```powershell
   Test-Path "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll"
   ```

3. Create `.vscode/launch.json` with a `dbgeng` configuration:

   ```json
   {
     "version": "0.2.0",
     "configurations": [
       {
         "name": "Debug my program",
         "type": "dbgeng",
         "request": "launch",
         "program": "${workspaceFolder}/build/Debug/myapp.exe",
         "stopAtEntry": true
       }
     ]
   }
   ```

4. Set a breakpoint in your source and press `F5`.

With the [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
extension installed you can omit `program`; the adapter uses the active CMake launch target.

For a ten-minute walkthrough see [Getting started](https://svnscha.github.io/dap-dbgeng/getting-started/),
and the scenario guides for [attach](https://svnscha.github.io/dap-dbgeng/scenarios/attach/),
[remote](https://svnscha.github.io/dap-dbgeng/scenarios/remote-debugging/), and
[kernel driver](https://svnscha.github.io/dap-dbgeng/scenarios/driver-debugging/) debugging.

## Configuration

Common options (full [launch reference](https://svnscha.github.io/dap-dbgeng/reference/launch/)
and [attach reference](https://svnscha.github.io/dap-dbgeng/reference/attach/)):

| Option | Applies to | Description |
| --- | --- | --- |
| `program` | launch | Executable to debug. Optional with CMake Tools (uses the launch target). |
| `args` | launch | Arguments: a single string or an array. |
| `cwd` | launch | Working directory; defaults to the program's directory. |
| `stopAtEntry` | launch / attach | Break at the entry point (launch) or on attach. |
| `processId` | attach | PID to attach to. Use `"${command:dap-dbgeng.pickProcess}"` to pick interactively. |
| `dumpFile` | attach | Open a crash dump (`.dmp`) instead of a live process. |
| `connectionString` | attach | A `dbgsrv` remote string, or a kernel transport when `kernel` is true. |
| `kernel` | attach | Kernel-mode debugging. |
| `dbgengPath` | both | Path to a specific `dbgeng.dll`. Omit to auto-detect. |
| `symbolPath` | both | Symbol (PDB) search paths. |
| `sources` | both | Source search directories. |

Extension setting:

- `dap-dbgeng.adapterPath` - absolute path to a `dap-dbgeng.exe` to use instead of the
  bundled adapter. Leave empty to use the bundled one.

## Learn more

- Documentation: [svnscha.github.io/dap-dbgeng](https://svnscha.github.io/dap-dbgeng/)
- Source and issues: [github.com/svnscha/dap-dbgeng](https://github.com/svnscha/dap-dbgeng)
- Release notes: [CHANGELOG.md](https://github.com/svnscha/dap-dbgeng/blob/main/vscode/CHANGELOG.md)

## License

[MIT](https://github.com/svnscha/dap-dbgeng/blob/main/LICENSE).
