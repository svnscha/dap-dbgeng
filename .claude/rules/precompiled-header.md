---
paths:
  - "src/**/*.h"
  - "src/**/*.cpp"
  - "tests/**/*.h"
  - "tests/**/*.cpp"
---

# Precompiled header - the one convention to remember

`src/stdafx.h` is force-included into **every** translation unit (via
`target_precompile_headers`). It is the **single include point** for the common dependencies:
`windows.h` (with `WIN32_LEAN_AND_MEAN` + `NOMINMAX`), `dbgeng.h` / `dbghelp.h`, the common STL,
and `fmt` / `nlohmann/json` / `spdlog`.

Therefore: **do not re-include those headers anywhere** - not in project headers, not in sources.
Project headers here are intentionally *not* self-contained; they assume the PCH.

- Need a **common** standard header that isn't in the PCH yet? Add it to `stdafx.h`, not to an
  individual file.
- Only include locally what the PCH genuinely does not carry: **project headers**
  (`"core/channel.h"`), **test-only** headers (`<gtest/gtest.h>`), and **target-specific** headers
  (e.g. `<spdlog/sinks/stdout_color_sinks.h>` in `main.cpp`).

The generated protocol library (`src/protocol/`) propagates its own PCH (`dap_service.h`, which
includes `protocol.h`) `PUBLIC` to everything that links it, so consumers neither `#include` the
protocol nor re-parse the large generated header per translation unit.
