# Debug a running process

Connect to a process **already running** on this machine, by its process ID. Use
`"request": "attach"`.

```json title=".vscode/launch.json"
{
  "name": "Attach to myapp",
  "type": "dbgeng",
  "request": "attach",
  "processId": 12345
}
```

That's a complete local attach (the adapter finds `dbgeng.dll` automatically). To
choose the process at debug time, set
`"processId": "${command:dap-dbgeng.pickProcess}"`. The picker lists local
processes; when a `connectionString` is also set it lists processes on the
`dbgsrv` host instead, so the same setting works for remote attach.

To catch a program that is not running yet, replace `processId` with
[`processName`](../reference/attach.md#processname): the adapter waits for a
process with that image name and attaches as soon as it appears.

For all attach options, see **[attach attributes](../reference/attach.md)**.
Attaching over a network or to a driver uses the same `attach` request:

- **[Debug a remote process](remote-debugging.md)** - a process on another machine.
- **[Debug a Windows driver](driver-debugging.md)** - kernel-mode drivers.
