# Troubleshooting

When a session won't start or behaves oddly, work through these common causes.
The fastest first move is to **check the logs**:

- The **dap-dbgeng** output channel (View -> Output -> "dap-dbgeng") shows which
  adapter was launched and any startup error.
- The adapter's own diagnostics go to **standard error**, which VS Code surfaces
  in the **Debug Console**.

For a full record of the DAP conversation, set a
[`trace`](reference/launch.md#trace) path in the configuration.

---

## The session won't start at all

### “Cannot find debug adapter” / unknown debug type `dbgeng`

VS Code doesn't have the extension that provides the `dbgeng` debug type.

**Fix:** install the **Native Windows Debugging (dbgeng)** extension (it bundles the
adapter - see [Getting started](getting-started.md)), make sure it's enabled, and
reload the window. Your configuration's `type` must be `dbgeng`.

### The adapter starts but immediately exits

Usually `dbgeng.dll` could not be loaded: either no Debugging Tools for Windows is
installed for the adapter to auto-resolve, or a `dbgengPath` you set points at a
missing or wrong file.

**Fix:** install the Debugging Tools for Windows (the adapter then finds
`dbgeng.dll` automatically), or set `dbgengPath` to a real `dbgeng.dll`:

```powershell
Test-Path "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
```

Remember JSON path rules: forward slashes or doubled backslashes.

### “Invalid configuration” / a required field is missing

`launch` requires `program`. `attach` requires one of `processId`, `processName`,
`dumpFile`, or `kernel` with a `connectionString`. `dbgengPath` is optional
(auto-resolved when omitted).

**Fix:** check your configuration against the
[launch](reference/launch.md) / [attach](reference/attach.md) reference.

---

## Breakpoints and source

### Breakpoints show as hollow / “unverified” and never hit

The debugger can't match your breakpoint to loaded code - almost always a
**symbols** problem.

**Fix:**

- Build your target in **Debug** so PDBs are produced.
- For attach sessions, add the PDB location to `symbolPath`.
- Make sure the binary running is the **same build** as your symbols.

### Stepping shows disassembly instead of my source

The debugger has symbols but can't find the **source file**.

**Fix:** add the folder containing your source to the `sources` array, e.g.
`["${workspaceFolder}/src"]`. List every root that holds code you step into.

---

## Attach problems

### Attach fails with access denied

You don't have permission to debug the target - common when it runs elevated or
as another user.

**Fix:** start VS Code **as administrator** so the adapter inherits the rights to
attach.

### Wrong process / PID changed

A hard-coded `processId` is stale because the process restarted.

**Fix:** look up the current PID (`Get-Process`, Task Manager) or use a
`${input:...}` variable to be prompted each time. See
[Debug a running process](scenarios/attach.md).

### “No process named … appeared within …”

`processName` attach gave up waiting. Either nothing started the process, it is
named differently, or it starts on a different machine than the adapter looks at.

**Fix:**

- Confirm the name matches the image name exactly, including `.exe`.
- If a hook is supposed to start it, check that the hook ran (see below) - for a
  service, the start belongs in `onAttachRequest`, not `afterConfigurationDone`.
- With `connectionString` set, the process must appear on the `dbgsrv` host.
- A slow starter may need a longer
  [`processNameTimeout`](reference/attach.md#processnametimeout).

---

## Target hooks (deploy and start)

Every hook command, its output, and its duration are logged in the **dap-dbgeng**
output channel. Read it first: it shows the exact command line that ran, so you
can paste it into a terminal and iterate there.

### A hook failed and the session did not start

A `beforeSession` command exited non-zero, which aborts the session on purpose
(no point debugging a stale binary).

**Fix:** run the logged command in a terminal to see the full error. The bundled
scripts need the OpenSSH client on `PATH`, key-based auth (they use
`BatchMode`, so they never prompt), and an SSH user who is an administrator on
the target.

### “service … is not registered on …”

`Deploy-Binary.ps1` derives the destination from the service registration, and
it never creates services itself.

**Fix:** register it once on the target, then F5 again:

```cmd
sc.exe create myservice type= kernel binPath= C:\myservice.sys
```

### “test signing is OFF …”

The deploy succeeded, but the target rejects test-signed drivers, so the service
start will fail.

**Fix:** on the throwaway target, `bcdedit /set testsigning on` and reboot.

### The service starts, but breakpoints in it never hit

The start ran too early - before breakpoints were applied.

**Fix:** start a kernel driver from `afterConfigurationDone` (breakpoints are
armed at that point) and a user-mode service from `onAttachRequest`. See
[target hooks](reference/attach.md#target).

---

## Remote problems

### Can't connect to the process server

The adapter can't reach `dbgsrv` on the target.

**Fix:**

- Confirm `dbgsrv` is running on the target: `dbgsrv -t tcp:port=5005`, or let a
  `Ensure-ProcessServer.ps1` hook start it (see
  [target hooks](reference/attach.md#target)).
- Confirm the port is open through the target's firewall.
- Test reachability from the host: `Test-NetConnection TARGETPC -Port 5005`.
- Make sure the `port` in your `connectionString` matches `dbgsrv`'s, and
  `server=` is the right hostname/IP.

### “This debug engine cannot be used for remote (dbgsrv) debugging”

The engine could not load its own `dbghelp.dll`, which it needs for
process-server connections. The **Store version of WinDbg** is the usual cause:
it does not allow loading DLLs out of its install directory, so the engine ends
up with the older `dbghelp.dll` from System32 and the connection fails.

**Fix:** install the **Debugging Tools for Windows** (a Windows SDK / WDK
feature) and either omit [`dbgengPath`](reference/attach.md#dbgengpath), so the
adapter finds them, or point it at that `dbgeng.dll`. Local and kernel
debugging with the Store version are unaffected.

### Connected, but symbols/lines are wrong

With `dbgsrv`, symbols resolve **locally**. The PDBs in your `symbolPath` must
match the exact binaries on the target.

**Fix:** point `symbolPath` at the matching build's PDBs (and/or a symbol
server). See [Debug a remote process](scenarios/remote-debugging.md).

---

## Driver (kernel) problems

### The target never connects / no break-in

The kernel transport on the two ends doesn't match, or the target wasn't rebooted
after enabling debugging.

**Fix:**

- Re-check `bcdedit /dbgsettings` on the target and **reboot** it.
- The `connectionString` `port`/`key` must **exactly** match the target's
  `bcdedit` settings.
- For VM serial pipes, confirm the named pipe name on both the VM config and the
  `com:` connection string.
- If Secure Boot is enabled, disable it in the VM firmware so debug settings take
  effect.

### My test driver won't load

Unsigned drivers are blocked by default.

**Fix:** on the throwaway target, enable test signing
(`bcdedit /set testsigning on`, then reboot) and disable Memory Integrity / HVCI,
or test-sign the driver. See `test-targets/sys/README.md` in the repository.

---

## Still stuck? Capture a trace

If you need help, record the session and attach the trace to your report:

```json
"trace": "${workspaceFolder}/recordings/session.json"
```

Reproduce the problem, then share the resulting JSON file along with your
`launch.json` configuration and the `debug`-level log. The trace captures the
exact protocol exchange, which makes problems much faster to diagnose.
