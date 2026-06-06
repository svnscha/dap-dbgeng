#pragma once

namespace dap_dbgeng::transport
{
// Records inbound and outbound DAP messages to a JSON trace file. Messages are
// buffered in memory from the start and flushed to the configured path on
// destruction. Tracing is opt-in — a session that never sets a path calls
// discard() to drop the buffer so a normal run does not retain the transcript.
//
// On-disk format so the replay fixtures and Normalize-DapRecording.ps1 keep matching:
//   { "version": 1, "messages": [ { "direction": "in"|"out", "message": {...} } ] }
// Adjacent console `output` events are merged into one (output concatenated).
//
// Thread-safe: every public operation takes an internal mutex.
class dap_trace_recorder
{
  public:
    // Construct a deferred recorder: it buffers from the start and writes
    // nothing until the first launch/attach request resolves a destination (or
    // discards the buffer). See resolve_trace_path_once_locked.
    dap_trace_recorder() = default;
    ~dap_trace_recorder();

    dap_trace_recorder(const dap_trace_recorder &) = delete;
    dap_trace_recorder &operator=(const dap_trace_recorder &) = delete;

    // Record an inbound message (as received from the client).
    void record_input(const nlohmann::json &message);

    // Record an outbound message (as sent to the client, after seq assignment).
    void record_output(const nlohmann::json &message);

    // Activate (or re-point) recording to `path`. Returns false and leaves the
    // recorder unchanged if the path cannot be prepared (e.g. it is a directory
    // or its parent cannot be created). Buffered messages are kept and flushed
    // on destruction.
    bool try_set_path(const std::filesystem::path &path);

    // Permanently stop recording and release the buffered transcript. Used when
    // a session opts out of tracing so the messages are not retained in memory.
    void discard();

  private:
    // The first launch/attach request decides tracing for the whole session (it
    // carries the adapter configuration, including the optional "trace" path):
    // set the destination, or — when no valid trace is configured — discard the
    // buffered transcript so a normal run does not retain it. Caller holds the mutex.
    void resolve_trace_path_once_locked(const nlohmann::json &message);

    // Buffer one direction/message pair, merging into a preceding console
    // `output` event when applicable. Caller holds the mutex.
    void record_locked(const char *direction, nlohmann::json message);

    // True when `direction`/`message` is a console `output` event whose body has
    // exactly { category: "console", output: <string> }; fills `output`.
    static bool try_get_console_output(const char *direction, const nlohmann::json &message, std::string &output);

    // Flush the buffered transcript to the configured path. Caller holds the
    // mutex.
    void write_trace_file_locked();

    struct trace_entry
    {
        std::string direction;
        nlohmann::json message;
    };

    mutable std::mutex mutex_;
    std::vector<trace_entry> messages_;
    std::optional<std::filesystem::path> path_;
    bool disposed_ = false;
    bool discarded_ = false;
    bool resolved_ = false;
};
} // namespace dap_dbgeng::transport
