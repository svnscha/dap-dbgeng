# Native Windows Debugging (dbgeng)

Debug **native Windows code** - C and C++ programs, services, and even the Windows
**kernel** - straight from Visual Studio Code, using the same engine that powers WinDbg.

You set breakpoints, step through code, and inspect variables with the familiar VS Code
debugging UI, while the real work is done by the Windows debug engine (`dbgeng`)
underneath - no fragile text scraping of WinDbg output.

[Documentation](https://svnscha.github.io/dap-dbgeng/) - [Getting started](https://svnscha.github.io/dap-dbgeng/getting-started/) - [Configuration reference](https://svnscha.github.io/dap-dbgeng/reference/launch/)

<!-- A short demo GIF goes well here once one is recorded. -->

## What you can do

- **[Debug a local program](https://svnscha.github.io/dap-dbgeng/scenarios/local-debugging/)** - launch an `.exe` under the debugger, break at entry, step, and inspect locals.
- **[Debug a running process](https://svnscha.github.io/dap-dbgeng/scenarios/attach/)** - attach to a program that is already running, by its process ID.
- **[Debug a remote process](https://svnscha.github.io/dap-dbgeng/scenarios/remote-debugging/)** - debug a process on another machine over the network with a `dbgsrv` process server.
- **[Debug a Windows driver](https://svnscha.github.io/dap-dbgeng/scenarios/driver-debugging/)** - attach to a kernel-debug-enabled target (a VM or second machine) for kernel-mode debugging.
- **Open a crash dump** - post-mortem debugging of a `.dmp` file.

## Before you start

You need a 64-bit Windows machine with:

- **Visual Studio Code** and this extension. The debug adapter is **bundled inside it**,
  so there is nothing else to build, download, or point at.
- **Debugging Tools for Windows**, which provides `dbgeng.dll` (usually
  `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll`). Install it through
  the Windows SDK or WDK installer (tick *Debugging Tools for Windows*). The adapter finds
  it automatically; set `dbgengPath` only to pin a specific copy.
- **PDB symbols** for the code you want to step through.

> Quick check that the engine is present:
> `Test-Path "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll"`

## Your first debugging session

1. Open your C++ workspace and create `.vscode/launch.json` with a `dbgeng`
   configuration:

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

2. Click the editor gutter to set a **breakpoint**.
3. Press **F5**. With `stopAtEntry: true` the debugger pauses at the program's entry
   point; otherwise it runs until it hits your breakpoint.
4. Use the debug toolbar to **continue** (F5), **step over** (F10), **step into** (F11),
   and **step out** (Shift+F11). Inspect locals and the call stack in the side panels,
   use the **Watch** pane, and evaluate expressions in the **Debug Console**.

That is a full local debugging loop. For the guided, ten-minute version see
**[Getting started](https://svnscha.github.io/dap-dbgeng/getting-started/)**.

## Common configurations

Each block below is one configuration inside the `configurations` array of
`.vscode/launch.json`. Full detail in the
[launch](https://svnscha.github.io/dap-dbgeng/reference/launch/) and
[attach](https://svnscha.github.io/dap-dbgeng/reference/attach/) reference.

**Launch a program**

```json
{
  "name": "Debug myapp",
  "type": "dbgeng",
  "request": "launch",
  "program": "${workspaceFolder}/build/Debug/myapp.exe",
  "args": "--config dev input.txt",
  "stopAtEntry": true
}
```

**Launch with CMake Tools** - with the
[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
extension installed you can omit `program`; the adapter uses the active launch target
(and builds it, prompting you to pick one if needed):

```json
{
  "name": "Debug (CMake target)",
  "type": "dbgeng",
  "request": "launch"
}
```

**Attach to a running process** - `${command:dap-dbgeng.pickProcess}` opens a process
picker at debug time:

```json
{
  "name": "Attach to a process",
  "type": "dbgeng",
  "request": "attach",
  "processId": "${command:dap-dbgeng.pickProcess}"
}
```

**Debug a remote process** - run `dbgsrv -t tcp:port=5005` on the target, then:

```json
{
  "name": "Attach on TARGETPC",
  "type": "dbgeng",
  "request": "attach",
  "processId": 4321,
  "connectionString": "tcp:port=5005,server=TARGETPC"
}
```

**Debug a Windows driver (kernel)** - the target must be set up for kernel debugging
(`bcdedit /debug on`):

```json
{
  "name": "Kernel debug",
  "type": "dbgeng",
  "request": "attach",
  "kernel": true,
  "connectionString": "net:port=50005,key=1.2.3.4"
}
```

**Open a crash dump**:

```json
{
  "name": "Open dump",
  "type": "dbgeng",
  "request": "attach",
  "dumpFile": "${workspaceFolder}/crashes/app.dmp"
}
```

## Configuration attributes

The options you will reach for most (see the reference for the full set and defaults):

| Attribute | Request | Description |
| --- | --- | --- |
| `program` | launch | Executable to launch. Optional with CMake Tools (uses the launch target). |
| `args` | launch | Command-line arguments: a single string or an array. |
| `cwd` | launch | Working directory; defaults to the program's directory. |
| `stopAtEntry` | launch / attach | Break at the entry point on launch (default `false`), or break in on attach (default `true`). |
| `processId` | attach | PID to attach to. Use `"${command:dap-dbgeng.pickProcess}"` to pick interactively. |
| `connectionString` | attach | A `dbgsrv` string for remote attach, or a kernel transport when `kernel` is `true`. |
| `kernel` | attach | Set `true` for kernel / driver debugging. |
| `dumpFile` | attach | Open a crash dump (`.dmp`) instead of attaching to a live process. |
| `symbolPath` | both | Symbol (PDB) search paths (local folders or a symbol-server string). |
| `sources` | both | Folders searched for source files (defaults to the workspace root). |
| `dbgengPath` | both | Path to a specific `dbgeng.dll`. Omit to auto-resolve. |

Extension setting:

- `dap-dbgeng.adapterPath` - absolute path to a `dap-dbgeng.exe` to use instead of the
  bundled adapter. Leave empty to use the bundled one.

## What is supported

Line and conditional breakpoints; step over / into / out, including instruction-level
stepping in the Disassembly view; call stack with on-demand frame loading; variables,
scopes, and registers, plus set-variable; expression evaluation in the Watch pane and
Debug Console; and the Disassembly view. See
[Supported features](https://svnscha.github.io/dap-dbgeng/reference/features/) for the
full, honest list of what is and is not supported.

## Learn more

- Documentation and guides: [svnscha.github.io/dap-dbgeng](https://svnscha.github.io/dap-dbgeng/)
- Troubleshooting: [svnscha.github.io/dap-dbgeng/troubleshooting](https://svnscha.github.io/dap-dbgeng/troubleshooting/)
- Source and issues: [github.com/svnscha/dap-dbgeng](https://github.com/svnscha/dap-dbgeng)
- Release notes: [CHANGELOG.md](https://github.com/svnscha/dap-dbgeng/blob/main/vscode/CHANGELOG.md)

## License

[MIT](https://github.com/svnscha/dap-dbgeng/blob/main/LICENSE).
