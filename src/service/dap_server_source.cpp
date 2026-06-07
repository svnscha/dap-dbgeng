#include "service/dap_server.h"

#include "util/string_utils.h"

namespace dap_dbgeng::service
{
// VS Code issues a `source` request to fetch source content for a frame whose
// Source it cannot open by path itself. The adapter resolves frames to on-disk
// paths, so serve the file content directly when a path is available.
void dap_server::handle_source_request(const protocol::SourceRequest &request)
{
    const std::optional<protocol::Source> &source = request.arguments.source;
    const std::optional<std::string> path = source.has_value() ? source->path : std::nullopt;

    if (util::is_blank(path))
    {
        send_error_response(request.seq, request.command,
                            "Source content is unavailable: the source has no path to read.");
        return;
    }

    std::ifstream stream(*path, std::ios::binary);
    if (!stream)
    {
        send_error_response(request.seq, request.command, fmt::format("Could not open source file '{}'.", *path));
        return;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();

    protocol::SourceResponse response;
    response.body.content = buffer.str();
    send_response(request.seq, request.command, std::move(response));
}
} // namespace dap_dbgeng::service
