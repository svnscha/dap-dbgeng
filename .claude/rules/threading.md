---
paths:
  - "src/service/**"
  - "src/debugger/**"
  - "src/core/**"
  - "src/transport/**"
  - "src/main.cpp"
---

# Architecture notes (threading)

The design is shaped by **dbgeng thread-affinity**, not by throughput:

- The DAP transport runs on the **main thread** (reads stdin, dispatches requests). All debug
  engine work runs on one dedicated **dispatcher thread** that alone owns `IDebugClient` and
  receives engine callbacks; handlers reach it through `util::debugger_session_dispatcher::invoke`.
  Only `IDebugControl::SetInterrupt` (pause) is called cross-thread, off the dispatcher.
- Outbound order is total: every response and event is pushed onto a single FIFO
  `core::channel<nlohmann::json>` drained by one writer thread, so wire order is deterministic.
- A wait-for-session-event loop pumps `WaitForEvent` on the dispatcher; engine callbacks set a
  pending-stopped state and queue `stopped`/`thread`/`exited`/`terminated` events. The coordinator
  for this lives in `service/dap_server_session_events.cpp`.
- **stdout is the DAP transport** - never log to it. All logging goes to **stderr** (spdlog
  `stderr_color_mt`).
