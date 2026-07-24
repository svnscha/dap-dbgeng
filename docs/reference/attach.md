# attach attributes

These attributes apply when `"request": "attach"` - the debugger connects to a
process that already exists, a remote target, or the kernel. The same request
covers three scenarios:

- [Debug a running process](../scenarios/attach.md) (local)
- [Debug a remote process](../scenarios/remote-debugging.md) (via `dbgsrv`)
- [Debug a Windows driver](../scenarios/driver-debugging.md) (`kernel: true`)

**Required:** none on its own - supply `processId`, `dumpFile`, or
(`kernel` + `connectionString`) depending on the scenario.

How the key fields combine determines the scenario:

| Scenario | `processId` | `connectionString` | `kernel` |
| --- | --- | --- | --- |
| Local attach | the local PID | omit | `false` |
| Remote attach | the PID on the target | a `dbgsrv` string | `false` |
| Kernel attach | omit | a kernel transport | `true` |

To open a crash dump instead of attaching to a live process, set
[`dumpFile`](#dumpfile).

## Attributes

| Attribute | Required | Description |
| --- | --- | --- |
| [`processId`](#processid) | - | PID to attach to (local, or on the `dbgsrv` host). |
| [`processName`](#processname) | - | Attach by executable name; polls until the process appears. |
| [`processNameTimeout`](#processnametimeout) | - | Wait budget for `processName` (default 15000 ms). |
| [`dumpFile`](#dumpfile) | - | Open a crash dump instead of attaching. |
| [`connectionString`](#connectionstring) | - | Remote (`dbgsrv`) or kernel transport string. |
| [`kernel`](#kernel) | - | Set `true` for kernel / driver debugging. |
| [`dbgengPath`](#dbgengpath) | - | Path to `dbgeng.dll`; auto-resolved when omitted. |
| [`stopAtEntry`](#stopatentry) | - | Break in on connect (default `true`). |
| [`symbolPath`](#symbolpath) | - | Extra locations to load PDB symbols from. |
| [`sources`](#sources) | - | Folders searched for source files. |
| [`trace`](#trace) | - | Record the DAP session to a file. |
| [`target`](#target) | - | Start/stop a driver service around the session (extension-only). |

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

### `processName`

- **Type:** string · Optional (alternative to `processId`)

Attach by **executable name** instead of PID. The adapter polls (locally, or on
the `dbgsrv` host when `connectionString` is set) until a matching process
appears, then attaches - made for processes that are just spawning, like a
service being started. See
[Debug a remote process](../scenarios/remote-debugging.md#attach-by-name-debug-a-remote-service).

```json
"processName": "myservice.exe"
```

---

### `processNameTimeout`

- **Type:** integer (milliseconds) · Optional
- **Default:** `15000`

How long to wait for `processName` to appear before the attach fails.

---

### `dumpFile`

- **Type:** string · Optional

Path to a crash dump (`.dmp`) to open for post-mortem debugging instead of
attaching to a live process. Mutually exclusive with `processId`.

```json
"dumpFile": "${workspaceFolder}/crashes/app.dmp"
```

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

- **Type:** string · Optional

The path to the **local** `dbgeng.dll` (the debug engine runs on your machine even
for remote and kernel sessions). **You usually do not need to set this.** When
omitted, the adapter resolves the engine automatically:

1. a `dbgeng.dll` bundled next to the adapter, then
2. the installed Windows SDK Debugging Tools
   (`...\Windows Kits\10\Debuggers\<arch>\dbgeng.dll`).

Set it only to point at a specific `dbgeng.dll`. If no engine can be found, the
session fails with a clear error.

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

For kernel attach, `false` means the machine keeps running once the session is
configured - the right setting together with [`target`](#target), where the
driver should load while the debugger watches.

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
- **Default:** `["${workspaceRoot}"]`

Folders to search when resolving source files. Same meaning as in launch
configurations.

---

### `trace`

- **Type:** string · Optional (omit to disable)

A file path to record the DAP session to, for diagnostics and replay fixtures.
Omit to record nothing.

```json
"trace": "${workspaceFolder}/recordings/attach.session.json"
```

---

### `target`

- **Type:** object · Optional
- Handled by the VS Code extension; the adapter ignores it.

Command **hooks** around the session: ordered lists of PowerShell command
lines, each bound to a session-lifecycle moment. There is no built-in behavior
beyond running them - what F5 does is exactly what the config says. The
easiest start is a configuration snippet ("Add Configuration..." offers the
kernel-driver and remote-service templates prefilled with the commands below).

```json
"target": {
  "host": "user@testbox",
  "hooks": {
    "beforeSession": [
      "${dbgengScripts}/Sign-Driver.ps1 -Binary '${workspaceFolder}/build/Debug/hello.sys' -CertSubject 'CN=my-test-signer' -OutCertificate '${workspaceFolder}/build/test-signer.cer'",
      "${dbgengScripts}/Deploy-Binary.ps1 -HostName '${host}' -Binary '${workspaceFolder}/build/Debug/hello.sys' -CertificateFile '${workspaceFolder}/build/test-signer.cer' -ServiceName hello -CheckTestSigning"
    ],
    "afterConfigurationDone": [
      "${dbgengScripts}/Start-RemoteService.ps1 -HostName '${host}' -ServiceName hello"
    ],
    "afterSessionEnd": [
      "${dbgengScripts}/Stop-RemoteService.ps1 -HostName '${host}' -ServiceName hello"
    ]
  }
}
```

The four hooks (the timing is the extension's job; the commands are yours):

| Hook | When | Typical use |
| --- | --- | --- |
| `beforeSession` | After the `preLaunchTask`, before the adapter starts. A failure aborts the session. | Sign, deploy, ensure `dbgsrv`. |
| `onAttachRequest` | The attach request is on the wire. | Start a user-mode service, so the adapter's [`processName`](#processname) poll finds the spawning process. |
| `afterConfigurationDone` | Breakpoints are armed. | Start a kernel driver service - a `DriverEntry` breakpoint hits. |
| `afterSessionEnd` | The session ended. Failures only log. | Teardown: stop services. |

`host` is just the value of the `${host}` token - typically the SSH
destination the bundled scripts take as `-HostName`.

**The bundled scripts** are general-purpose, parameterized PowerShell,
recommended as building blocks but in no way required - any command line works,
including fully custom scripts. `${dbgengScripts}` expands to their folder
inside hook commands; anywhere else (e.g. tasks.json) use
`${command:dap-dbgeng.scriptsPath}`.

| Script | Purpose |
| --- | --- |
| `Sign-Driver.ps1 -Binary <file> -CertSubject <CN=...> -OutCertificate <cer>` | Test-sign locally: finds signtool, creates/reuses the self-signed cert, exports it. |
| `Deploy-Binary.ps1 -HostName <host> -Binary <file> -ServiceName <name>` (or `-Destination <path>`) `[-CertificateFile <cer>] [-CheckTestSigning]` | One-SSH-round-trip deploy: stages the file, derives the destination from the service registration, stops the service, replaces the binary, trusts the cert. |
| `-HostName localhost` | Both of the above run on this machine instead, doing the same steps without SSH. |
| `Start-RemoteService.ps1` / `Stop-RemoteService.ps1 -HostName <host> -ServiceName <name>` | `sc.exe start` / `stop` over SSH. |
| `Ensure-ProcessServer.ps1 -HostName <host> -Transport <spec> [-DbgsrvPath <path>]` | Starts `dbgsrv` on the target when none is running, copying the debugger to `~\.dap-dbgeng\tools` there on first use (about 33 MB) unless `-DbgsrvPath` points at one already on the target. |

The bundled scripts talk SSH (OpenSSH client on `PATH`, key auth, the SSH user
an administrator on the target) - the only requirement, and only theirs:
custom hook commands can use any transport. Commands signal failure by
throwing or exiting non-zero; each command, its output, and its duration are
logged in the dap-dbgeng output channel.

Two palette commands work the hooks outside F5: **Run Target Hook** (pick a
target and hook) and **Redeploy and Restart Target** (also in the debug
toolbar during a session) - runs `afterSessionEnd`, then `beforeSession`, then
the start hooks, i.e. the edit-rebuild-redeploy inner loop without detaching
the debugger.

## Examples

### Local attach

```json
{
  "name": "Attach to myapp",
  "type": "dbgeng",
  "request": "attach",
  "processId": "${command:dap-dbgeng.pickProcess}"
}
```

### Remote attach

```json
{
  "name": "Attach on TARGETPC",
  "type": "dbgeng",
  "request": "attach",
  "processId": 4321,
  "connectionString": "tcp:port=5005,server=TARGETPC"
}
```

### Kernel attach

```json
{
  "name": "Kernel debug",
  "type": "dbgeng",
  "request": "attach",
  "kernel": true,
  "connectionString": "net:port=50005,key=1.2.3.4"
}
```

### Open a crash dump

```json
{
  "name": "Open dump",
  "type": "dbgeng",
  "request": "attach",
  "dumpFile": "${workspaceFolder}/crashes/app.dmp"
}
```
