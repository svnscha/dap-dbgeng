# Use cases

There are four ways to start a debug session. They differ only in the
`launch.json` configuration - once running, debugging (breakpoints, stepping,
variables) is identical. Pick the one that matches what you're doing:

| Use case | `request` | Key fields | Use it when… |
| --- | --- | --- | --- |
| **[Debug a local program](local-debugging.md)** | `launch` | `program` | You want the debugger to **start** a program on this machine. |
| **[Debug a running process](attach.md)** | `attach` | `processId` | The program is **already running** on this machine. |
| **[Debug a remote process](remote-debugging.md)** | `attach` | `processId` + `connectionString` | The program runs on **another machine** (via `dbgsrv`). |
| **[Debug a Windows driver](driver-debugging.md)** | `attach` | `kernel: true` + `connectionString` | You're debugging **kernel-mode drivers** on a debug-enabled target. |

!!! tip "Launch vs. attach"
    **Launch** = the debugger creates the process. **Attach** = it connects to one
    that already exists (locally, remotely, or a whole machine for drivers).

Remote work has two more options: a `launch` configuration with a
`connectionString` starts the program **on the target machine**, and
[`processName`](../reference/attach.md#processname) attaches by executable name
instead of a PID, waiting for the process to appear - which is what makes
catching a service as it starts work.

Any configuration can also carry a [`target`](../reference/attach.md#target)
block: command hooks that deploy and start what you are debugging as part of F5,
and tear it down afterwards.

Every configuration also accepts shared options like `dbgengPath`, `sources`,
`stopAtEntry`, and `trace` - see the
**[configuration reference](../reference/launch.md)** for all of them.
