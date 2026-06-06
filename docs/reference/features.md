# Supported features

The adapter implements a focused, dependable subset of the Debug Adapter
Protocol. This page tells you what works so you know what to expect in the
VS Code UI - and what *isn't* available, so you don't go hunting for a button
that won't appear.

## What works

| Feature | Notes |
| --- | --- |
| **Line breakpoints** | Set them in the editor gutter. |
| **Conditional breakpoints** | Break only when an expression is true. |
| **Step over / into / out** | Plus **continue** and **pause**. |
| **Instruction-level stepping** | Stepping works in the **Disassembly view** too. |
| **Call stack** | With on-demand (delayed) frame loading for responsiveness. |
| **Variables & scopes** | Inspect locals, arguments, and registers/scopes. |
| **Set variable** | Edit a variable's value from the Variables pane. |
| **Evaluate expressions** | In the **Watch** pane and **Debug Console**. |
| **Disassembly view** | View and step through disassembly. |
| **Terminate & disconnect** | End the session, or detach and leave the target running. |

## What is *not* (yet) supported

These DAP features are **not** advertised today, so VS Code won't offer them.
Items marked **Planned** map cleanly onto the Windows debug engine and are on the
roadmap; the rest are out of scope for now.

| Feature | Status |
| --- | --- |
| Function breakpoints | **Planned** - `dbgeng` breaks on symbols natively |
| Hit-conditional breakpoints (break after N hits) | **Planned** - engine breakpoints carry a pass count |
| Data breakpoints (watchpoints) | **Planned** - `dbgeng` break-on-access (`ba`) |
| Instruction breakpoints | **Planned** - breakpoints by address |
| Logpoints | **Planned** - adapter-side, like conditional breakpoints |
| Read / write memory | **Planned** - engine read/write virtual memory |
| Set expression | **Planned** - engine evaluator can assign |
| Modules view | **Planned** - engine enumerates loaded modules |
| Evaluate on hover | **Planned** - same path as the Watch pane |
| Value formatting options (hex toggle, etc.) | **Planned** - engine formats hex/decimal |
| Reverse debugging / step back | **Not supported** - would require Time Travel Debugging traces |
| Restart frame | **Not supported** |
| Single-thread (per-thread) execution | **Not supported** - execution is session-wide |

!!! note "Execution is session-wide"
    When you continue, step, or pause, it applies to the whole debugged session,
    not to one thread in isolation. This matches how the underlying engine
    operates, and in kernel mode the "threads" you see are processor contexts.

## Why a narrow set?

The adapter deliberately exposes only what it implements correctly against the
Windows debug engine, rather than advertising features that would behave
unpredictably. If a capability isn't listed under *What works*, assume the
corresponding VS Code affordance won't be available - that's by design, not a
misconfiguration. A **Planned** capability is still in this category today: the
engine can do it, but the adapter doesn't advertise it yet.

!!! tip "Need a missing capability?"
    Many low-level operations are still reachable by typing native debugger
    commands into the **Debug Console** / **Watch** expressions, which the
    adapter evaluates through the engine. For anything else, check the project's
    issue tracker.
