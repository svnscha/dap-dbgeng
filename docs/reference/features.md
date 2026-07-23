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
| **Function breakpoints** | Add by name in the **Breakpoints** view, e.g. `myapp!main`. |
| **Data breakpoints** | **Break on Value Change** on a variable (hardware watchpoints). |
| **Instruction breakpoints** | Set in the **Disassembly view** gutter. |
| **C++ exception breakpoints** | Tick **C++ exceptions** to break on first-chance throws. |
| **Step over / into / out** | Plus **continue** and **pause**. |
| **Instruction-level stepping** | Stepping works in the **Disassembly view** too. |
| **Call stack** | With on-demand (delayed) frame loading for responsiveness. |
| **Variables & scopes** | Inspect locals, arguments, and registers/scopes. |
| **Set variable** | Edit a variable's value from the Variables pane. |
| **Set expression** | Edit a watch value from the **Watch** pane. |
| **Evaluate expressions** | Watch entries are C++ expressions (`t.origin.x`); the **Debug Console** takes native debugger commands. |
| **Read / write memory** | **View Binary Data** on a variable; see the note below. |
| **Disassembly view** | View and step through disassembly. |
| **Terminate & disconnect** | End the session, or detach and leave the target running. |

!!! note "Viewing and editing memory needs the Hex Editor extension"
    **View Binary Data** is provided by Microsoft's
    [Hex Editor](https://marketplace.visualstudio.com/items?itemName=ms-vscode.hexeditor)
    extension. To save an edit back to memory, switch the editor to **Replace**
    mode first (click the **Insert** indicator in the status bar). Insert-mode
    edits resize the file, which memory does not support, so saving fails with
    "Not supported".

## What is *not* (yet) supported

These DAP features are **not** advertised today, so VS Code won't offer them.
Items marked **Planned** map cleanly onto the Windows debug engine and are on the
roadmap; the rest are out of scope for now.

| Feature | Status |
| --- | --- |
| Hit-conditional breakpoints (break after N hits) | **Planned** - engine breakpoints carry a pass count |
| Logpoints | **Planned** - adapter-side, like conditional breakpoints |
| Modules view | **No VS Code UI** - the adapter answers the `modules` request, but VS Code has no Modules view to show it |
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
    commands into the **Debug Console**, which the adapter passes to the engine.
    A **Watch** entry that does not resolve as a C++ expression is also executed
    as a native command. For anything else, check the project's issue tracker.
