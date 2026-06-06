# dap-dbgeng

[![CI](https://github.com/svnscha/dap-dbgeng/actions/workflows/ci.yml/badge.svg)](https://github.com/svnscha/dap-dbgeng/actions/workflows/ci.yml)
[![Docs](https://github.com/svnscha/dap-dbgeng/actions/workflows/pages.yml/badge.svg)](https://svnscha.github.io/dap-dbgeng/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)

A Windows [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) server
backed by the Windows debug engine (`dbgeng` / `dbghelp`). It lets DAP clients such as VS Code
debug native Windows user-mode processes (launch, attach, remote attach) and kernel targets.
C++20, CMake + Ninja, vcpkg manifest mode. Windows-only.

## Highlights

- **Launch, attach, remote (`dbgsrv`), and kernel** debugging through one adapter.
- **Breakpoints** (line + conditional), **stepping** (over/into/out, instruction-level),
  **call stack**, **variables/scopes/registers**, **set variable**, **disassembly view**, and
  **expression evaluation** via the engine.
- **Native DbgEng APIs** throughout - no fragile text scraping of WinDbg output.
- A built-in **session-trace recorder** that doubles as the project's replay-test format.

See [Supported features](docs/reference/features.md) for the full, honest list of what is and
isn't advertised.

## Documentation

Published at **[svnscha.github.io/dap-dbgeng](https://svnscha.github.io/dap-dbgeng/)** from
[`docs/`](docs) on every push to `main`. Build it locally with `npm run docs` (live preview) or
`npm run docs:build`:

- [Getting started](docs/getting-started.md)
- Scenarios: [local](docs/scenarios/local-debugging.md) ·
  [attach](docs/scenarios/attach.md) · [remote](docs/scenarios/remote-debugging.md) ·
  [driver](docs/scenarios/driver-debugging.md)
- Reference: [launch](docs/reference/launch.md) · [attach](docs/reference/attach.md) ·
  [features](docs/reference/features.md)

Contributors: see [CONTRIBUTING.md](CONTRIBUTING.md) and [CLAUDE.md](CLAUDE.md) (architecture and
conventions).

## Layout

```
.
├── CMakeLists.txt        # vcpkg toolchain detection, deps, subdirectories
├── CMakePresets.json     # Ninja configure + debug/release build/test presets
├── vcpkg.json            # Dependencies: fmt, nlohmann-json, spdlog, gtest
├── package.json          # npm task runner (build/test/generate/...)
├── src/
│   ├── stdafx.h          # Precompiled header (windows.h, dbgeng, STL, fmt/json/spdlog)
│   ├── main.cpp          # Entrypoint
│   ├── core/             # Threading primitives: channel, task_queue, event_sink
│   ├── protocol/         # Generated DAP types + dispatch (see Protocol below)
│   ├── transport/        # stdio framing, trace recorder
│   ├── service/          # dap_server + one file per DAP request handler
│   ├── debugger/         # dbgeng COM wrapper (native DbgEng APIs), split by theme
│   └── util/             # argument reader, dispatcher, factory, command classifier
├── tests/                # GoogleTest; tests/replay drives recorded sessions out-of-process
├── test-targets/         # native debuggees the tests use (testapp) + kernel driver (sys)
├── protgen/              # Python generator that emits src/protocol from the DAP schema
├── scripts/              # build-env, formatting, and trace tooling
└── vscode/               # VS Code extension manifest
```

The single product target is `dap-dbgeng.exe`. Production code lives in an internal CMake
`OBJECT` library so the test binary links the exact same objects without shipping a second
artifact.

## Build

The Ninja build needs the MSVC toolchain. The npm scripts enter the Visual Studio developer
shell automatically, so they run from any shell:

```powershell
npm run configure   # configure (Ninja, auto-finds vcpkg)
npm run build       # build Debug
```

Equivalently, from an "x64 Native Tools for VS" prompt (so `cl`/`link` are on PATH):

```powershell
cmake --preset windows-x64
cmake --build --preset windows-debug
```

vcpkg is auto-discovered from `$env:VCPKG_ROOT`, a vendored `./vcpkg`, or the bundled VS 2022 copy.

## Test

```powershell
npm test            # ctest --preset windows-debug
npm run test:replay # just the recorded-session replay tests
npm run test:release # configure the release tree first (npm run configure:release), then ctest
```

The test debuggees under `test-targets/testapp` always build unoptimized with full
debug info (even in Release), so their line tables and locals stay stable for the
recorded sessions and source-line assertions. Tests resolve the adapter and
debuggees relative to the test binary, so both the Debug and Release trees run
end-to-end.

The suite is unit/integration GoogleTests plus out-of-process replay tests that spawn
`dap-dbgeng.exe` and replay recorded sessions against the `test-targets/testapp` debuggees
(built as part of the tree). Tests that need `dbgeng.dll` or the native debuggees skip cleanly
when those are unavailable.

## Protocol

`src/protocol/` is generated from `protgen/schema/debugAdapterProtocol.json`. Regenerate with
`npm run generate` rather than hand-editing.

## Kernel test driver

`test-targets/sys` is a WDK "Hello World" kernel driver for manually exercising kernel-mode
attach. It is a separate Visual Studio + WDK build (it does not build under Ninja and is not part
of the automated tests): `npm run build:sys`, or configure the main tree with the VS generator and
`-DDAP_DBGENG_BUILD_KERNEL_DRIVER=ON`. See `test-targets/sys/README.md`.

## Format

```powershell
pwsh scripts/Format.ps1            # changed files only (git-aware)
pwsh scripts/Format.ps1 -Path src
```

## Contributing

Bugs and feature requests go through the
[issue tracker](https://github.com/svnscha/dap-dbgeng/issues/new/choose) (templates guide what to
include). For code changes, run `npm run check` and see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE).
