#pragma once

// Out-of-process recorded-session replay harness. Drives recorded fixtures
// against a freshly spawned dap-dbgeng.exe over stdio.

namespace dap_dbgeng::replay
{
// A single recorded message entry: its direction ("in" / "out") and the JSON
// message body.
struct recorded_message
{
    std::string direction;
    nlohmann::json message;
};

// A loaded fixture: the de-normalized (token-substituted) message list.
struct recorded_session
{
    std::vector<recorded_message> messages;
};

// The result of a replay attempt: every message the adapter emitted (all) and
// the subset that are not `output` events (non_output), in arrival order.
struct replay_result
{
    std::vector<nlohmann::json> all;
    std::vector<nlohmann::json> non_output;
};

// Thrown by the replay loop when a recorded expectation does not match the
// adapter output (the retry wrapper catches it). Other failures (skip
// conditions) are signalled separately.
struct replay_assertion_error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Signals a precondition that should turn into GTEST_SKIP. Carries the skip reason.
struct replay_skip : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Environment resolution. Each thrower raises replay_skip with a reason when the
// resource is unavailable.
// ---------------------------------------------------------------------------

// Repository root: nearest ancestor of the test module directory containing
// both `src` and `test-targets`. Used for ${workspaceFolder} and default paths.
std::filesystem::path repository_root_or_skip();

// dbgeng.dll path (DAP_DBGENG_WINDBG_PATH or the default Windows Kits path).
std::string dbgeng_path_or_skip();

// The built C++ adapter (DAP_DBGENG_EXE or the default CMake build output).
std::filesystem::path adapter_path_or_skip();

// The native launch / attach targets (DAP_DBGENG_NATIVE_APP or default).
std::filesystem::path launch_target_path_or_skip();
std::filesystem::path attach_target_path_or_skip();

// dbgsrv.exe next to dbgeng.dll.
std::filesystem::path process_server_path_or_skip();

// ---------------------------------------------------------------------------
// Fixture loading.
// ---------------------------------------------------------------------------

// Absolute path of a fixture by file name, resolved under DAP_REPLAY_FIXTURE_DIR.
std::filesystem::path fixture_path(const std::string &file_name);

// Raw fixture text (pre-substitution); used by the Requires* scanners.
std::string read_fixture_text(const std::string &file_name);

bool requires_attach_target(const std::string &file_name);
bool requires_process_server(const std::string &file_name);

// Load + de-normalize a fixture, applying ${token} -> value text substitutions
// before JSON parsing.
recorded_session load_session(const std::string &file_name, const std::map<std::string, std::string> &substitutions);

// ---------------------------------------------------------------------------
// Replay entrypoint: runs the fixture against a fresh adapter with the 2x retry
// wrapper. Throws replay_skip
// for unmet preconditions; the caller maps that to GTEST_SKIP.
// ---------------------------------------------------------------------------
replay_result replay(const std::string &file_name, int timeout_milliseconds = 10000);

} // namespace dap_dbgeng::replay
