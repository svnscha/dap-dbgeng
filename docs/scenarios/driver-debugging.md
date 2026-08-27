# Debug a Windows driver

Debug **kernel-mode drivers** (and the OS around them). Driver debugging is
whole-machine, so you need two: a **host** (VS Code + adapter) and a **target**
machine being debugged - almost always a VM.

<div class="video-embed">
  <iframe src="https://www.youtube-nocookie.com/embed/m1E5AsglKiQ" title="Debug a Windows kernel driver with dap-dbgeng" loading="lazy" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen></iframe>
</div>

```mermaid
flowchart LR
    subgraph host["HOST MACHINE"]
        A["VS Code + dap-dbgeng<br/>dbgeng.dll runs HERE"]
    end
    subgraph target["TARGET MACHINE (VM)"]
        B["Windows, booted with<br/>kernel debugging enabled"]
    end
    A <-->|"KDNET / serial / pipe"| B
```

!!! danger "Use a throwaway target"
    Kernel debugging halts the whole target at breakpoints and weakens its
    security posture. Use a disposable VM or dedicated test box.

## 1. Enable kernel debugging on the target

From an elevated prompt on the target, then **reboot**:

```cmd
bcdedit /debug on
bcdedit /dbgsettings net hostip:<HOST-IP> port:50005 key:1.2.3.4
```

## 2. Configure `launch.json` on the host

```json title=".vscode/launch.json"
{
  "name": "Debug driver (KDNET)",
  "type": "dbgeng",
  "request": "attach",
  "kernel": true,
  "connectionString": "net:port=50005,key=1.2.3.4"
}
```

- `kernel: true` selects kernel mode and makes `connectionString` a kernel
  transport.
- `connectionString` must match the target's `bcdedit` settings. There is no
  `processId` - the session is the whole machine.

### Connection string by transport

| Transport | `connectionString` |
| --- | --- |
| Network (KDNET) | `net:port=50005,key=1.2.3.4` |
| COM / serial (VM named pipe) | `com:port=\\.\pipe\kd,baud=115200,pipe,reconnect` |
| 1394 (FireWire) | `1394:channel=32` |
| USB | `usb:targetname=mytarget` |

See **[attach attributes](../reference/attach.md)** for all options. A full worked
example (build, load, and break on a driver) lives in `test-targets/sys/README.md`
in the repository.

## 3. Load the driver on F5 (optional)

If the target is reachable over SSH and your driver service is registered
there, add `target.hooks` so one F5 builds (your `preLaunchTask`), test-signs
and deploys the driver over the registered service binary, attaches with the
machine kept running, starts the service once breakpoints are armed, and hits
`DriverEntry`. The "Add Configuration..." snippet **dbgeng: Kernel driver**
prefills this:

```json title=".vscode/launch.json"
{
  "name": "Debug driver (KDNET)",
  "type": "dbgeng",
  "request": "attach",
  "kernel": true,
  "connectionString": "net:port=50005,key=1.2.3.4",
  "stopAtEntry": false,
  "preLaunchTask": "Build driver",
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
}
```

Every hook entry is a plain PowerShell command line - drop the sign command
when your build signs already, or swap any of them for your own scripts. The
bundled `Deploy-Binary.ps1` derives the destination from the service
registration on the target, so this works for almost any driver unchanged.
Hooks and the bundled scripts are documented in
**[attach attributes](../reference/attach.md#target)**.
