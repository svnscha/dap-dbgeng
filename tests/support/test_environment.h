#pragma once

namespace dap_dbgeng::test_support
{
// Reads an environment variable; std::nullopt when unset or empty.
std::optional<std::string> env_var(const char *name);

// Directory holding the running test executable (the CMake binaryDir/tests),
// so build artifacts resolve relative to whichever tree is under test (Debug,
// Release, ...) rather than a hard-coded path.
std::filesystem::path module_directory();

// Walks up from the test module's directory (then the working directory) until a
// directory containing both `src` and `CMakePresets.json` is found.
std::optional<std::filesystem::path> find_repository_root();

// Resolves dbgeng.dll from DAP_DBGENG_WINDBG_PATH or the default SDK location.
// Returns an empty string when unavailable (the caller should skip).
std::string resolve_dbgeng_path();

// Resolves the launched native test app (test_launch.exe) from
// DAP_DBGENG_NATIVE_APP or the build tree. Empty when unavailable.
std::string resolve_launch_target_path();

// Directory holding the launch target, or empty when the target is unavailable.
std::string resolve_launch_target_directory();

// Resolves the launch target's source (test-targets/testapp/main.cpp). Empty
// when unavailable.
std::string resolve_launch_target_source();
} // namespace dap_dbgeng::test_support

// Skips the current test (GTEST_SKIP) with `reason` when `value` is empty.
#define DAP_REQUIRE_OR_SKIP(value, reason)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((value).empty())                                                                                           \
        {                                                                                                              \
            GTEST_SKIP() << (reason);                                                                                  \
        }                                                                                                              \
    } while (false)
