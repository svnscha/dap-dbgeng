# Getting started

This page gets you from nothing to your **first breakpoint** in a local program.
It takes about ten minutes. Later pages cover the other use cases (running,
remote, and driver debugging) - but the setup here applies to all of them.

## 1. Check the prerequisites

You need a 64-bit Windows machine with:

- **Visual Studio Code** installed.
- **The Debug Adapter for WinDbg extension** - it bundles the adapter, so there's
  nothing separate to build or download. You install it in step 2.
- **Debugging Tools for Windows**, which ships `dbgeng.dll`. The usual location is:

    ```text
    C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll
    ```

    If you don't have it, install it through the **Windows SDK** or **WDK**
    installer (tick *Debugging Tools for Windows*), or grab the standalone
    package. You normally don't need to note the path: the adapter finds
    `dbgeng.dll` automatically from the installed SDK. Set `dbgengPath` only to use
    a specific copy.

!!! tip "Quick sanity check"
    Confirm `dbgeng.dll` exists before you start:

    ```powershell
    Test-Path "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbgeng.dll"
    ```

## 2. Install the VS Code extension

Install the **Debug Adapter for WinDbg** extension. The adapter executable is
**bundled inside it**, so there's nothing else to download, build, or point at -
once the extension is installed, you're ready to debug.

Install it from a packaged `.vsix`:

```powershell
code --install-extension vscode-dap-dbgeng.vsix
```

The extension registers the `windbg` debug type that your `launch.json` uses.

## 3. Create a `launch.json`

In your project, open the **Run and Debug** view (++ctrl+shift+d++) and click
*create a launch.json file*, or create `.vscode/launch.json` by hand. Use the
`windbg` type:

```json title=".vscode/launch.json"
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug my program",
      "type": "windbg",
      "request": "launch",
      "target": "${workspaceFolder}/build/Debug/myapp.exe",
      "stopAtEntry": true,
      "sources": [
        "${workspaceFolder}"
      ]
    }
  ]
}
```

What each line means (full details in the [reference](reference/launch.md)):

- **`type`** - always `windbg` for this adapter.
- **`request`** - `launch` to start a new program, or `attach` to connect to one
  that's already running.
- **`target`** - the program you want to debug.
- **`stopAtEntry`** - `true` so the debugger pauses at the program's entry point,
  giving you a chance to set breakpoints before anything runs.
- **`sources`** - folders the debugger searches to show your source code (defaults
  to the workspace root).
- **`dbgengPath`** - optional; the path to `dbgeng.dll`. Omit it to let the adapter
  find the engine automatically (see the [reference](reference/launch.md#dbgengpath)).

!!! tip "Use forward slashes or escaped backslashes"
    JSON treats `\` as an escape character. Write paths with forward slashes
    (`C:/Path/To/File`) or doubled backslashes (`C:\\Path\\To\\File`).

## 4. Start debugging

1. Open one of your source files and click in the gutter to set a **breakpoint**
   (a red dot).
2. Press ++f5++ (or pick your configuration in the Run and Debug view and click
   the green arrow).
3. The program launches under the debugger. With `stopAtEntry: true` it pauses
   immediately; otherwise it runs until it hits a breakpoint.
4. Use the debug toolbar to **continue** (++f5++), **step over** (++f10++),
   **step into** (++f11++), and **step out** (++shift+f11++). Inspect locals and
   the call stack in the side panels, hover-free watch expressions in the
   **Watch** pane, and the **Debug Console** for evaluating expressions.

That's a full local debugging loop.

## Where to go next

- **[Debug a local program](scenarios/local-debugging.md)** - the launch scenario in depth, including arguments and working directory.
- **[Debug a running process](scenarios/attach.md)** - connect to something that's already running.
- **[Debug a remote process](scenarios/remote-debugging.md)** - debug a process on another machine.
- **[Debug a Windows driver](scenarios/driver-debugging.md)** - kernel-mode driver debugging.
- **[Configuration reference](reference/launch.md)** - every option, explained.
- **[Troubleshooting](troubleshooting.md)** - when something doesn't work.
