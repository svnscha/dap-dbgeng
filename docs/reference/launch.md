# launch attributes

These attributes apply when `"request": "launch"` - the debugger **starts** a
program for you. See [Debug a local program](../scenarios/local-debugging.md) for
a guided walkthrough.

**Required:** `program`, unless the [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
extension is installed and a launch target is selected (then it is used automatically).

## Attributes

| Attribute | Required | Description |
| --- | --- | --- |
| [`program`](#program) | Yes* | Path to the executable to launch (*optional with CMake Tools). |
| [`args`](#args) | - | Command-line arguments (string or array). |
| [`cwd`](#cwd) | - | Working directory for the program. |
| [`dbgengPath`](#dbgengpath) | - | Path to `dbgeng.dll`; auto-resolved when omitted. |
| [`stopAtEntry`](#stopatentry) | - | Break at the entry point (default `false`). |
| [`sources`](#sources) | - | Folders searched for source files. |
| [`symbolPath`](#symbolpath) | - | Symbol (PDB) search paths. |
| [`trace`](#trace) | - | Record the DAP session to a file. |

## Details

### `program`

- **Type:** string · Required (see below)

The path to the executable to launch under the debugger.

```json
"program": "${workspaceFolder}/build/Debug/myapp.exe"
```

`program` is optional when the **CMake Tools** extension (`ms-vscode.cmake-tools`)
is installed: if you omit it, the adapter defaults to CMake Tools' **launch
target** via `${command:cmake.launchTargetPath}`, and VS Code resolves it - building
it and prompting you to pick one if none is selected yet. Without CMake Tools
installed, `program` is required and the session fails with a message to set it.

---

### `args`

- **Type:** string or array of strings · Optional

Command-line arguments passed to the program: either a **single string** written
exactly as you would type it on a command line, or an **array** of individual
arguments.

```json
"args": "--config dev --verbose input.txt"
```

---

### `cwd`

- **Type:** string · Optional
- **Default:** the program's directory

The working directory (current directory) for the debugged program. When omitted,
the engine uses the directory containing `program`. When the program is auto-resolved
from CMake Tools, the working directory defaults to
`${command:cmake.launchTargetDirectory}` (the launch target's directory).

```json
"cwd": "${workspaceFolder}/build/Debug"
```

---

### `dbgengPath`

- **Type:** string · Optional

The path to the `dbgeng.dll` debug engine library. **You usually do not need to
set this.** When omitted, the adapter resolves the engine automatically:

1. a `dbgeng.dll` bundled next to the adapter, then
2. the installed Windows SDK Debugging Tools
   (`...\Windows Kits\10\Debuggers\<arch>\dbgeng.dll`).

Set it only to point at a specific `dbgeng.dll`. If no engine can be found, the
session fails with a clear error.

```json
"dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
```

---

### `stopAtEntry`

- **Type:** boolean · Optional
- **Default:** `false`

Break at the program's **entry point** right after launch.

- `true` - pause at entry before any of your code runs, so you can set
  breakpoints in very-early code.
- `false` - the program runs immediately and stops only at breakpoints you've
  set (the adapter resumes the target after configuration is done).

```json
"stopAtEntry": true
```

---

### `sources`

- **Type:** array of strings · Optional
- **Default:** `["${workspaceRoot}"]`

Folders to search when resolving source files for your code. List every root that
contains code you want to step through.

```json
"sources": [
  "${workspaceFolder}/src",
  "${workspaceFolder}/lib"
]
```

---

### `symbolPath`

- **Type:** array of strings · Optional

Symbol (PDB) search paths passed to the engine. Each entry may be a local folder
or a symbol-server string (for example `srv*C:\symbols*https://msdl.microsoft.com/download/symbols`).

```json
"symbolPath": [
  "${workspaceFolder}/build/Debug"
]
```

---

### `trace`

- **Type:** string · Optional (omit to disable)

A file path to **record the entire DAP session** to, for diagnostics and for
building replay fixtures. Omit this field to record nothing (the default). The
recording is written in the adapter's `{version, messages}` trace format.

```json
"trace": "${workspaceFolder}/recordings/session.json"
```

!!! note
    Tracing is opt-in. With no `trace` set, the adapter buffers nothing - there's
    no performance or disk cost.

## Full example

```json title=".vscode/launch.json"
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug myapp",
      "type": "windbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/Debug/myapp.exe",
      "args": "--config dev input.txt",
      "stopAtEntry": true,
      "sources": ["${workspaceFolder}"]
    }
  ]
}
```
