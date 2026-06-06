---
paths:
  - "src/**/*.h"
  - "src/**/*.cpp"
  - "tests/**/*.h"
  - "tests/**/*.cpp"
  - "test-targets/**/*.h"
  - "test-targets/**/*.cpp"
---

# C++ style

- **Formatting.** clang-format, Microsoft base, 4-space indent, 120 columns (`.clang-format`). Run
  `pwsh scripts/Format.ps1` (git-aware, changed files only) before committing; `npm run
  format:check` verifies without modifying. Line endings are LF (`.gitattributes`).
- **Warnings.** Our targets (`dap-dbgeng-objects`, `dap-dbgeng`, `dap-dbgeng-tests`) build at
  `/W4 /WX` - a new warning fails the build. The generated `src/protocol/` and third-party code are
  exempt (set on their own targets), so keep hand-written code warning-clean.
- Match the surrounding code's idiom and naming (`lower_case` for types/functions/variables,
  trailing `_` on private members) - see `.clang-tidy`.
