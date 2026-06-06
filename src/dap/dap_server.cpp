#include "dap/dap_server.h"

namespace dap_dbgeng
{
std::string frame_message(const nlohmann::json &message)
{
    const std::string body = message.dump();
    return fmt::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
}
} // namespace dap_dbgeng
