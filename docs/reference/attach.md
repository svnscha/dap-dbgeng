# attach attributes

These attributes apply when `"request": "attach"` - the debugger connects to a
process that already exists, a remote target, or the kernel. The same request
covers three scenarios:

- [Debug a running process](../scenarios/attach.md) (local)
- [Debug a remote process](../scenarios/remote-debugging.md) (via `dbgsrv`)
- [Debug a Windows driver](../scenarios/driver-debugging.md) (`kernel: true`)

**Required:** `dbgengPath`.

How the key fields combine determines the scenario:

| Scenario | `processId` | `connectionString` | `kernel` |
| --- | --- | --- | --- |
| Local attach | the local PID | omit | `false` |
| Remote attach | the PID on the target | a `dbgsrv` string | `false` |
| Kernel attach | omit | a kernel transport | `true` |

## Attributes

| Attribute | Required | Description |
| --- | --- | --- |
| [`dbgengPath`](#dbgengpath) | Yes | Path to `dbgeng.dll` (the debug engine). |
| [`processId`](#processid) | - | PID to attach to (local, or on the `dbgsrv` host). |
| [`connectionString`](#connectionstring) | - | Remote (`dbgsrv`) or kernel transport string. |
| [`kernel`](#kernel) | - | Set `true` for kernel / driver debugging. |
| [`stopAtEntry`](#stopatentry) | - | Break in on connect (default `true`). |
| [`symbolPath`](#symbolpath) | - | Extra locations to load PDB symbols from. |
| [`sources`](#sources) | - | Folders searched for source files. |
| [`workingDir`](#workingdir) | - | Working directory for the session. |
| [`verbosity`](#verbosity) | - | Adapter log level (default `info`). |
| [`trace`](#trace) | - | Record the DAP session to a file. |

## Details

### `processId`

- **Type:** number or string · Optional

The system process ID to attach to. For **remote** attach, this is the PID on the
machine running `dbgsrv`. Omit it for kernel debugging.

```json
"processId": 12345
```

To pick the process interactively at debug time, use the built-in picker:

```json
"processId": "${command:dap-dbgeng.pickProcess}"
```

The picker lists local processes, or - when `connectionString` is set - the
processes on the `dbgsrv` host, so the same setting covers local and remote attach.

---

### `connectionString`

- **Type:** string · Optional (omit for local attach)

The remote connection string. Its meaning depends on `kernel`:

- **User-mode remote attach** (`kernel: false`, with `processId`) - a `dbgsrv`
  **process-server** string. Start the server on the target with
  `dbgsrv -t tcp:port=5005`, then use:

    ```text
    tcp:port=5005,server=HOSTNAME
    ```

- **Kernel attach** (`kernel: true`) - a **kernel transport** string instead, for
  example:

    ```text
    net:port=50005,key=1.2.3.4
    com:port=\\.\pipe\kd,baud=115200,pipe,reconnect
    ```

Omit this field entirely for a local user-mode attach.

---

### `kernel`

- **Type:** boolean · Optional
- **Default:** `false`

Set to `true` for kernel debugging. When `true`, `connectionString` is
interpreted as a kernel transport rather than a `dbgsrv` string. Requires a
target configured for kernel debugging (e.g. `bcdedit /debug on`).

A kernel session is whole-machine and is never terminated: disconnecting just
drops the connection and leaves the target running.

```json
"kernel": true
```

---

### `dbgengPath`

- **Type:** string · **Required**
- **Default:** `C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll`

The path to the **local** `dbgeng.dll`. Even for remote and kernel sessions, the
debug engine runs on your machine.

---

### `stopAtEntry`

- **Type:** boolean · Optional
- **Default:** `true`

For attach, this controls whether the adapter forces the target to **break in
immediately** on connect.

- `true` (default) - break in right after attaching; the target is paused.
- `false` - leave the target running after connecting; it stops only at a
  breakpoint or when you pause.

```json
"stopAtEntry": false
```

---

### `symbolPath`

- **Type:** array of strings · Optional
- **Default:** `[]`

Extra locations to load PDB symbol files from. Especially useful for attach,
where the running binary's symbols may not be next to your source. Entries can be
local folders or a symbol-server string.

```json
"symbolPath": [
  "${workspaceFolder}/build/Debug",
  "srv*C:/symbols*https://msdl.microsoft.com/download/symbols"
]
```

---

### `sources`

- **Type:** array of strings · Optional
- **Default:** `["${workspaceRoot}/src"]`

Folders to search when resolving source files. Same meaning as in launch
configurations.

---

### `workingDir`

- **Type:** string · Optional
- **Default:** the workspace root (`${workspaceRoot}`)

The working directory for the debug session.

---

### `verbosity`

- **Type:** string · Optional
- **Default:** `info`
- **Values:** `debug`, `info`, `warn`, `error`, `fatal`

Adapter log verbosity (written to standard error). Raise to `debug` when
diagnosing a problem.

---

### `trace`

- **Type:** string · Optional (omit to disable)

A file path to record the DAP session to, for diagnostics and replay fixtures.
Omit to record nothing.

```json
"trace": "${workspaceFolder}/recordings/attach.session.json"
```

## Examples

### Local attach

```json
{
  "name": "Attach to myapp",
  "type": "windbg",
  "request": "attach",
  "processId": 12345,
  "dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
}
```

### Remote attach

```json
{
  "name": "Attach on TARGETPC",
  "type": "windbg",
  "request": "attach",
  "processId": 4321,
  "connectionString": "tcp:port=5005,server=TARGETPC",
  "dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
}
```

### Kernel attach

```json
{
  "name": "Kernel debug",
  "type": "windbg",
  "request": "attach",
  "kernel": true,
  "connectionString": "net:port=50005,key=1.2.3.4",
  "dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
}
```
