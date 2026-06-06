#pragma once

#include "core/channel.h"
#include "transport/dap_message_writer.h"

namespace dap_dbgeng::service
{
class dap_server;
}

namespace dap_dbgeng::transport
{
class dap_trace_recorder;

// DAP transport over blocking input/output streams:
//
//   - The inbound loop runs on the calling (main) thread in run(): read framed
//     messages from `in`, record them, dispatch requests to the server, and
//     stop when the server signals exit or the input closes.
//   - The outbound path is a single ordered queue (core::channel) drained by one
//     dedicated writer thread. send() enqueues; the writer assigns the wire seq
//     (monotonic from 1), records the trace, frames, and writes. FIFO over one
//     thread gives a total wire order, which keeps continued/stopped from racing later.
//
// The transport IS the message writer the server sends through.
class io_stream_server : public dap_message_writer
{
  public:
    // `recorder` is optional and not owned; pass nullptr to disable tracing.
    io_stream_server(std::istream &in, std::ostream &out, dap_trace_recorder *recorder = nullptr);
    ~io_stream_server() override;

    io_stream_server(const io_stream_server &) = delete;
    io_stream_server &operator=(const io_stream_server &) = delete;

    // Enqueue an outbound message onto the ordered writer queue.
    void send(nlohmann::json message) override;

    // Run the inbound request loop until the input closes or the server exits.
    // Starts the writer thread, drains it on shutdown so a final response/event
    // is written, then joins it.
    void run(service::dap_server &server);

  private:
    // Pop framed messages off the queue in order and write them to `out`.
    void writer_loop();

    // Read one framed DAP message from `in`. Returns std::nullopt on EOF.
    std::optional<nlohmann::json> read_message();

    std::istream &in_;
    std::ostream &out_;
    dap_trace_recorder *recorder_;

    core::channel<nlohmann::json> outbound_;
    std::thread writer_thread_;
    int next_sequence_ = 1;
};
} // namespace dap_dbgeng::transport
