# Contributing

Thanks for contributing to dap-dbgeng. This is a Windows-only C++20 project (CMake + Ninja +
vcpkg) - see [README.md](README.md) for an overview and [CLAUDE.md](CLAUDE.md) for the deeper
architecture and conventions.

## Reporting bugs and requesting features

Open an issue from the [issue chooser](https://github.com/svnscha/dap-dbgeng/issues/new/choose) -
the **Bug report** and **Feature request** forms ask for what's needed to act on it. For a bug,
the repro, environment versions, and adapter logs (stderr at `logLevel` `debug`, or a session
trace - set a `trace` path in the launch/attach config) matter most. Please search existing
issues first.

## Prerequisites

- Windows with Visual Studio 2022 (Desktop C++ workload) and the Windows SDK (for `dbgeng.dll`).
- Node.js (only for the `npm run` task runner) and Python 3 (for the protocol generator).
- The `npm` scripts enter the VS developer environment automatically, so they run from any shell.

## Workflow

```powershell
npm run configure     # configure (Ninja, vcpkg)
npm run build         # build Debug
npm test              # ctest: unit/integration + replay
npm run format        # clang-format changed files
npm run check         # format check + configure + build + test (run before opening a PR)
```

A Release tree builds and tests separately (`npm run configure:release && npm run build:release &&
npm run test:release`). CI runs both the Debug and Release matrices on `windows-latest`, so make
sure your change builds and passes in both.

Live integration and replay tests need `dbgeng.dll` and the built `test-targets` debuggees; they
skip cleanly when those are unavailable, so a green run with skips means the native prerequisites
were not present.

## Conventions

- **Precompiled header.** `src/stdafx.h` is force-included everywhere and is the single include
  point for `windows.h`/`dbgeng`/STL/`fmt`/`nlohmann-json`/`spdlog`. Do not re-include those in
  project headers or sources.
- **Generated protocol.** Everything in `src/protocol/` is produced by `protgen/protogen.py`.
  Change the generator (and run `npm run generate`), never the generated headers by hand.
- **Style.** clang-format (Microsoft base, 4-space, 120 cols); LF line endings. `npm run
  format:check` verifies without modifying.
- **Tests stay green.** Keep `npm test` passing; add tests with behavior changes. The replay
  fixtures under `tests/replay/data` are the out-of-process contract.

## Commits / PRs

- Keep commits focused with a clear message; run `npm run check` first.
- Describe behavior changes and how you verified them.
- Expect CI to build and test both Debug and Release; PRs should be green there.
- New `/W4 /WX` warnings fail the build - keep our targets warning-clean (the generated
  `src/protocol` and third-party code are exempt).
