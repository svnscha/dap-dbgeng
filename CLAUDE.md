# dap-dbgeng

A Windows [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) server
backed by the Windows debug engine (`dbgeng` / `dbghelp`). DAP-speaking clients (e.g. VS Code)
drive native Windows debugging through it. C++20, CMake + Ninja, vcpkg manifest mode. The single
product is `dap-dbgeng.exe`, a console app that speaks DAP over stdin/stdout.

## Build / test / format

The Ninja build needs the MSVC toolchain on the environment. The npm scripts enter the VS
developer shell automatically (via `scripts/With-DevEnv.ps1`), so they work from any shell:

```powershell
npm run configure     # cmake --preset windows-x64 (Ninja, auto-finds vcpkg)
npm run build         # cmake --build --preset windows-debug
npm test              # ctest --preset windows-debug  (unit + integration + replay)
npm run generate      # regenerate src/protocol from the DAP schema
npm run matrix        # regenerate the request coverage page (docs/development/request-coverage.md)
pwsh scripts/Format.ps1   # clang-format changed files (git-aware)
```

The request coverage matrix is generated, not hand-maintained: `npm run matrix` rewrites
`docs/development/request-coverage.md` from the dispatch, handlers, and replay fixtures, and CI runs
`npm run matrix:check` to fail if the committed page drifts. Regenerate and commit it after adding a
handler or a replay fixture.

Running `cmake`/`ctest` directly works too, but only from a VS developer environment (e.g. the
"x64 Native Tools" prompt) so `cl`/`link` are on PATH. Windows-only (dbgeng). vcpkg deps
(`vcpkg.json`): `fmt`, `nlohmann-json`, `spdlog`, `gtest`. vcpkg is pinned via `builtin-baseline`
in `vcpkg.json`; bump that commit deliberately to move dependency versions (the manifest lists no
per-package version constraints).

## Targets

Shared production code lives in an internal CMake `OBJECT` library (`dap-dbgeng-objects`) that
produces no standalone artifact; both `dap-dbgeng.exe` and the test binary (`dap-dbgeng-tests`)
link the same objects. Add production sources to `src/CMakeLists.txt`, test sources to
`tests/CMakeLists.txt`.

```
src/
  stdafx.h        precompiled header (see .claude/rules/precompiled-header.md)
  main.cpp        entrypoint: logger init, binary stdio, transport.run(server)
  core/           threading primitives: channel, task_queue, event_sink
  protocol/       generated DAP types + dispatch (protocol.h, dap_service.h) - generated, do not edit
  transport/      stdio framing server, trace recorder, message-writer interface
  service/        dap_server + one dap_server_<command>.cpp per DAP request
  debugger/       debugger_session (dbgeng COM, native DbgEng APIs) - split by theme across files
  util/           argument reader, message inspector, session dispatcher/factory, command classifier
tests/            GoogleTest, following the src/ layout; tests/replay drives recorded sessions
test-targets/     native debuggees the tests launch/attach to (testapp), kernel driver (sys)
protgen/          Python generator that emits src/protocol from the DAP schema
scripts/          PowerShell helpers (build env, formatting, trace tooling)
vscode/           VS Code extension manifest
```

## Conventions

Topic-scoped conventions live in `.claude/rules/` and load automatically when you read a matching
file:

- `precompiled-header.md` - `src/stdafx.h` is the single include point; never re-include
  windows/dbgeng/STL/fmt/json/spdlog. **The one convention to remember.** (`src/**`, `tests/**`)
- `generated-protocol.md` - `src/protocol/` is generated; change `protgen/protogen.py` and
  regenerate, never hand-edit. (`src/protocol/**`, `protgen/**`)
- `threading.md` - dbgeng thread-affinity, the dispatcher thread, total outbound order, and
  "stdout is the DAP transport - never log to it". (`src/service/**`, `src/debugger/**`, …)
- `cpp-style.md` - clang-format (Microsoft, 4-space, 120 cols, LF) and `/W4 /WX` on our targets.
- `testing.md` - the replay fixtures, the thread-event tolerance in the matcher, and the
  unoptimized test debuggees. (`tests/**`, `test-targets/**`)
- `markdown.md` - Markdown typography for every `*.md`: plain hyphens only (no em/en dashes), no
  emojis. Run `pwsh scripts/Format-Docs.ps1`. (`**/*.md`)
- `documentation.md` - authoring style for the `docs/` user guide (scenario-first, link to the
  reference, sentence-case). (`docs/**`)
