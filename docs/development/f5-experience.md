# F5 experience design

Status: implemented. This page records the design and its reasoning; the user
guide for the feature lives in [attach attributes](../reference/attach.md#target)
and the [driver](../scenarios/driver-debugging.md#3-load-the-driver-on-f5-optional) /
[remote process](../scenarios/remote-debugging.md#attach-by-name-debug-a-remote-service)
scenarios.

## Goal

One F5 does everything: build, sign, deploy to the target, attach the debugger,
arm breakpoints, then start the payload so entry breakpoints hit. No manual
"continue" click, no separate "start the driver" task. The same model covers
kernel drivers, remote user-mode processes, and remote services.

## The model: hooks + general-purpose scripts

The extension contributes exactly one mechanism: **hooks** - ordered lists of
PowerShell command lines in the launch config's `target.hooks`, each bound to a
session-lifecycle moment the extension alone can observe:

| Hook | Moment | Why it exists |
| --- | --- | --- |
| `beforeSession` | After the `preLaunchTask`, before the adapter starts. | Deploy must run after the build. VS Code runs both config-resolve provider stages BEFORE the `preLaunchTask`, so the only correct extension-side hook is the (async) debug adapter descriptor factory - resolve-time deploys would ship the previous build. A failure here aborts the session cleanly. |
| `onAttachRequest` | The attach request is on the wire (tracker `onWillReceiveMessage`). | A user-mode service must start now: the process has to exist for the adapter's `processName` poll, but not so early that startup is missed. |
| `afterConfigurationDone` | The configurationDone response (tracker `onDidSendMessage`). | Breakpoints - including an unresolved `DriverEntry` one - are armed at this exact point; a kernel driver started here stops at entry. |
| `afterSessionEnd` | `onDidTerminateDebugSession`. | Teardown. Failures log, never surface. |

There is deliberately no other behavior: no stage vocabulary, no derived
defaults, no hidden steps. What F5 does is what the config lists, and every
command, its output, and its duration are logged to the output channel. Earlier
iterations had structured sugar (`signing`/`deploy`/`run` blocks, then named
overridable stages); both were removed in favor of raw hooks - the sugar hid
the actual commands and doubled the customization surface. Discoverability
comes from configuration snippets ("Add Configuration..." offers prefilled
kernel-driver and remote-service templates) and the schema docs instead.

The commands' building blocks are **general-purpose parameterized PowerShell
scripts bundled with the extension** (`vscode/scripts/`), fully usable
standalone (CI, other editors, plain tasks.json):

- `Sign-Driver.ps1` - signtool discovery (workspace WDK NuGet layout, Windows
  Kits, user-level NuGet cache), self-signed cert create/reuse in
  `Cert:\CurrentUser\My`, sign, export the certificate.
- `Deploy-Binary.ps1` - the whole remote deploy in ONE SSH round trip (below).
- `Start-RemoteService.ps1` / `Stop-RemoteService.ps1` - `sc.exe` over SSH.
- `Ensure-ProcessServer.ps1` - start `dbgsrv` on the target when none runs.

Reachability: `${dbgengScripts}` expands inside hook commands (plus
`${host}` from `target.host`); everywhere else VS Code's
`${command:dap-dbgeng.scriptsPath}` variable resolves to the same folder. The
bundled scripts assume SSH (OpenSSH client, key auth, admin user) - that is
their requirement, not the mechanism's; custom hook commands can use anything.

Two palette commands reuse the same hooks outside F5: **Run Target Hook** and
**Redeploy and Restart Target** (afterSessionEnd, beforeSession, then the
start hooks - the inner dev loop without detaching the kernel debugger; also
in the debug toolbar).

## Deploy-Binary.ps1: one SSH round trip

The Win32-OpenSSH client has no ControlMaster, so every connection costs a
TCP+auth handshake plus a remote shell startup (about a second each); per-step
ssh/scp calls made early versions feel slow. Deploy-Binary.ps1 instead sends a
generated PowerShell script to the target via `-EncodedCommand`
(quoting-proof, default-shell-agnostic) and streams the binary and certificate
as base64 on stdin. On the target it: stages in `~\.dap-dbgeng\staging\`; resolves the
destination from `sc.exe qc BINARY_PATH_NAME` (normalizing quoted user-mode
paths, `\??\C:\...`, `\SystemRoot\...`, and the bare `system32\drivers\...`
form - a missing service fails with the one-time `sc.exe create` command);
stops the service (a running service's image file is locked); replaces the
file; trusts the certificate (Root + TrustedPublisher); reports markers
(`DEST=`, and `TESTSIGNING=` with `-CheckTestSigning`, which the extension
turns into a warning notification). Passing `localhost` as the host runs the
same steps without ssh, which is both a supported mode and how its behavior is
verified.

## Adapter capabilities this builds on

Added alongside the extension work, useful to any DAP client:

- Kernel attach honors `stopAtEntry: false`: no forced stopped event; the
  target resumes after configurationDone (the "click continue once" fix).
- `launch` accepts `connectionString`: the process is created through a dbgsrv
  process server (`CreateProcessAndAttach2`), so `program`/`cwd` are target
  paths and `stopAtEntry` works for remote launches.
- `attach` accepts `processName` (+ `processNameTimeout`): polls locally or on
  the process server until a matching process appears, then attaches - made
  for service processes that are just spawning.

## Out of scope / known limits

- The SCM spawns service processes, so a service attach lands early in startup
  rather than exactly at `ServiceMain`; IFEO-style deferred attach was
  considered and deliberately not built.
- WinRM / PowerShell remoting: a transport concern of the scripts now; a
  WinRM-based Deploy-Binary variant can be dropped in via hooks without
  touching the extension.
- A "Debug Targets" tree view was built and removed: a sidebar mirroring
  remote mutable state (service status over SSH) is a cache with no
  invalidation signal, stale by design. The palette commands carry the
  actions without that problem.
