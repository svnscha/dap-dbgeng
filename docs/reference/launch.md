# launch attributes

These attributes apply when `"request": "launch"` - the debugger **starts** a
program for you. See [Debug a local program](../scenarios/local-debugging.md) for
a guided walkthrough.

**Required:** `target`, `dbgengPath`.

## Attributes

| Attribute | Required | Description |
| --- | --- | --- |
| [`target`](#target) | Yes | Path to the executable to launch. |
| [`dbgengPath`](#dbgengpath) | Yes | Path to `dbgeng.dll` (the debug engine). |
| [`args`](#args) | - | Command-line arguments, as a single string. |
| [`workingDir`](#workingdir) | - | Working directory for the program. |
| [`stopAtEntry`](#stopatentry) | - | Break at the entry point (default `false`). |
| [`sources`](#sources) | - | Folders searched for source files. |
| [`verbosity`](#verbosity) | - | Adapter log level (default `info`). |
| [`trace`](#trace) | - | Record the DAP session to a file. |

## Details

### `target`

- **Type:** string · **Required**

The path to the executable to launch under the debugger.

```json
"target": "${workspaceFolder}/build/Debug/myapp.exe"
```

---

### `dbgengPath`

- **Type:** string · **Required**
- **Default:** `C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll`

The path to the `dbgeng.dll` debug engine library the adapter loads. This must
point at a real `dbgeng.dll` from the Debugging Tools for Windows.

```json
"dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
```

---

### `args`

- **Type:** string · Optional

Command-line arguments passed to the program, written as a **single string**
exactly as you would type them on a command line.

```json
"args": "--config dev --verbose input.txt"
```

---

### `workingDir`

- **Type:** string · Optional
- **Default:** the workspace root (`${workspaceRoot}`)

The working directory (current directory) for the debugged program.

```json
"workingDir": "${workspaceFolder}/build/Debug"
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
- **Default:** `["${workspaceRoot}/src"]`

Folders to search when resolving source files for your code. List every root that
contains code you want to step through.

```json
"sources": [
  "${workspaceFolder}/src",
  "${workspaceFolder}/lib"
]
```

---

### `verbosity`

- **Type:** string · Optional
- **Default:** `info`
- **Values:** `debug`, `info`, `warn`, `error`, `fatal`

How much diagnostic output the adapter writes to its log (on standard error - it
never pollutes the debug protocol stream). Raise it to `debug` when reporting a
problem.

```json
"verbosity": "debug"
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
      "target": "${workspaceFolder}/build/Debug/myapp.exe",
      "args": "--config dev input.txt",
      "workingDir": "${workspaceFolder}/build/Debug",
      "dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll",
      "stopAtEntry": true,
      "sources": ["${workspaceFolder}/src"],
      "verbosity": "info"
    }
  ]
}
```
