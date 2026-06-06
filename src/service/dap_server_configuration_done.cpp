#include "service/dap_server.h"

namespace dap_dbgeng::service
{
void dap_server::handle_configuration_done_request(const protocol::ConfigurationDoneRequest &request)
{
    send_response(request.seq, request.command, protocol::ConfigurationDoneResponse{});

    if (launch_awaiting_configuration_done_ && !launch_stop_at_entry_)
    {
        continue_after_configuration_done();
    }

    reset_launch_configuration_state();
}
} // namespace dap_dbgeng::service
