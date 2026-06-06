#pragma once

namespace dap_dbgeng::transport
{
// Outbound DAP sink. A single synchronous enqueue: `send` takes a fully-formed
// JSON message and hands it to the transport, which assigns the wire `seq`,
// records the trace, frames, and writes it out. Implementations must be safe to
// call from any thread (request-handler thread and dbgeng worker thread both send).
struct dap_message_writer
{
    virtual ~dap_message_writer() = default;

    // Enqueue a message for delivery to the client. The transport owns sequence
    // assignment, so callers leave `seq` unset (or <= 0).
    virtual void send(nlohmann::json message) = 0;
};
} // namespace dap_dbgeng::transport
