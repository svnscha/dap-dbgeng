#pragma once

namespace dap_dbgeng::core
{
// Outbound side of the threading split: the dbgeng session emits DAP events
// (stopped, output, terminated, ...) without knowing how they are delivered.
// The transport implements this by framing + writing to stdout under a lock;
// tests implement it by capturing events in memory. Implementations must be
// safe to call from the worker thread.
class event_sink
{
  public:
    virtual ~event_sink() = default;
    virtual void emit(const nlohmann::json &event) = 0;
};
} // namespace dap_dbgeng::core
