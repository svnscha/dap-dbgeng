#pragma once

namespace dap_dbgeng
{
// Wire framing for the Debug Adapter Protocol: messages are JSON payloads
// prefixed with a `Content-Length` header, separated from the body by a blank
// line (see https://microsoft.github.io/debug-adapter-protocol/overview).

// Serialize a DAP message to its on-the-wire form (header + body).
std::string frame_message(const nlohmann::json &message);
} // namespace dap_dbgeng
