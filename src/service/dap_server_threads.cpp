#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_threads_request(const protocol::ThreadsRequest &request)
{
    protocol::ThreadsResponse response;
    response.body.threads = get_threads();
    send_response(request.seq, request.command, std::move(response));
}
} // namespace dap_dbgeng::service
