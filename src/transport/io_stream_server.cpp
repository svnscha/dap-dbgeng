#include "transport/io_stream_server.h"

#include "dap/dap_server.h"
#include "service/dap_server.h"
#include "transport/dap_trace_recorder.h"

namespace dap_dbgeng::transport
{
namespace
{
// Parse the Content-Length value out of a DAP header block (header lines are
// CRLF separated, the prefix match is case-insensitive). Throws when the header
// is missing or malformed.
std::size_t parse_content_length(const std::string &header_text)
{
    constexpr std::string_view prefix = "content-length:";

    std::size_t line_start = 0;
    while (line_start < header_text.size())
    {
        std::size_t line_end = header_text.find("\r\n", line_start);
        if (line_end == std::string::npos)
        {
            line_end = header_text.size();
        }

        const std::string_view line(header_text.data() + line_start, line_end - line_start);
        if (!line.empty() && line.size() >= prefix.size())
        {
            std::string lowered(line.substr(0, prefix.size()));
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == prefix)
            {
                std::string_view value = line.substr(prefix.size());
                // Trim surrounding whitespace.
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                {
                    value.remove_prefix(1);
                }
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                {
                    value.remove_suffix(1);
                }

                try
                {
                    const long long parsed = std::stoll(std::string(value));
                    if (parsed >= 0)
                    {
                        return static_cast<std::size_t>(parsed);
                    }
                }
                catch (const std::exception &)
                {
                    // Fall through to the error below.
                }
            }
        }

        if (line_end == header_text.size())
        {
            break;
        }
        line_start = line_end + 2;
    }

    throw std::runtime_error("Missing Content-Length header.");
}
} // namespace

io_stream_server::io_stream_server(std::istream &in, std::ostream &out, dap_trace_recorder *recorder)
    : in_(in), out_(out), recorder_(recorder)
{
}

io_stream_server::~io_stream_server()
{
    // Defensive: if run() was not driven to completion, make sure the writer
    // thread does not outlive the server.
    outbound_.close();
    if (writer_thread_.joinable())
    {
        writer_thread_.join();
    }
}

void io_stream_server::send(nlohmann::json message)
{
    outbound_.push(std::move(message));
}

void io_stream_server::run(service::dap_server &server)
{
    writer_thread_ = std::thread([this] { writer_loop(); });

    while (true)
    {
        std::optional<nlohmann::json> message = read_message();
        if (!message.has_value())
        {
            break; // EOF / closed input.
        }

        if (recorder_ != nullptr)
        {
            recorder_->record_input(*message);
        }

        const std::string type = message->value("type", std::string{});
        if (type != "request")
        {
            continue; // Only requests drive the adapter.
        }

        if (server.handle_request(*message))
        {
            break; // Server asked to exit (e.g. disconnect/terminate).
        }
    }

    // Stop accepting new outbound messages and let the writer drain whatever is
    // already queued (a final response/terminated must still be written) before
    // joining.
    outbound_.close();
    if (writer_thread_.joinable())
    {
        writer_thread_.join();
    }
}

void io_stream_server::writer_loop()
{
    while (std::optional<nlohmann::json> message = outbound_.pop())
    {
        // Assign the wire sequence if the sender left it unset (missing, or a
        // non-positive placeholder).
        const auto seq = message->find("seq");
        const bool needs_sequence = seq == message->end() || !seq->is_number_integer() || seq->get<long long>() <= 0;
        if (needs_sequence)
        {
            (*message)["seq"] = next_sequence_++;
        }

        if (recorder_ != nullptr)
        {
            recorder_->record_output(*message);
        }

        const std::string framed = dap_dbgeng::frame_message(*message);
        out_.write(framed.data(), static_cast<std::streamsize>(framed.size()));
        out_.flush();
    }
}

std::optional<nlohmann::json> io_stream_server::read_message()
{
    // Read the header block one byte at a time until the CRLFCRLF terminator.
    std::string header;
    while (true)
    {
        const int next = in_.get();
        if (next == std::char_traits<char>::eof())
        {
            if (header.empty())
            {
                return std::nullopt; // Clean EOF between messages.
            }
            throw std::runtime_error("Unexpected end of stream while reading DAP headers.");
        }

        header.push_back(static_cast<char>(next));
        const std::size_t count = header.size();
        if (count >= 4 && header[count - 4] == '\r' && header[count - 3] == '\n' && header[count - 2] == '\r' &&
            header[count - 1] == '\n')
        {
            break;
        }
    }

    const std::size_t content_length = parse_content_length(header);

    std::string body(content_length, '\0');
    std::size_t total_read = 0;
    while (total_read < content_length)
    {
        in_.read(body.data() + total_read, static_cast<std::streamsize>(content_length - total_read));
        const std::streamsize bytes_read = in_.gcount();
        if (bytes_read <= 0)
        {
            throw std::runtime_error("Unexpected end of stream while reading a DAP payload.");
        }
        total_read += static_cast<std::size_t>(bytes_read);
    }

    return nlohmann::json::parse(body);
}
} // namespace dap_dbgeng::transport
