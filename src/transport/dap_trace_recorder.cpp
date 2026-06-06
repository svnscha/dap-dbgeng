#include "transport/dap_trace_recorder.h"

namespace dap_dbgeng::transport
{
namespace
{
// Report a trace failure: best-effort, never throws.
void report_failure(const std::string &message)
{
    try
    {
        spdlog::error("DAP trace recording failed: {}", message);
    }
    catch (...)
    {
    }
}

// Prepare a trace destination. Throws on failure (a directory at the path, or a
// parent that cannot be created).
std::filesystem::path prepare_trace_path(const std::filesystem::path &path)
{
    if (path.empty())
    {
        throw std::invalid_argument("Trace path must not be empty.");
    }

    const std::filesystem::path full_path = std::filesystem::absolute(path);
    if (std::filesystem::is_directory(full_path))
    {
        throw std::runtime_error(fmt::format("Trace path '{}' is a directory.", full_path.string()));
    }

    const std::filesystem::path directory = full_path.parent_path();
    if (!directory.empty())
    {
        std::filesystem::create_directories(directory);
    }

    return full_path;
}

// True when `message` is a DAP request with the given command.
bool is_request_with_command(const nlohmann::json &message, std::string_view command)
{
    if (!message.is_object())
    {
        return false;
    }
    const auto type = message.find("type");
    if (type == message.end() || !type->is_string() || type->get<std::string>() != "request")
    {
        return false;
    }
    const auto command_value = message.find("command");
    return command_value != message.end() && command_value->is_string() && command_value->get<std::string>() == command;
}

// The "trace" string under the request's "arguments" object, if present and a
// non-empty string.
std::optional<std::string> trace_path_from_request(const nlohmann::json &message)
{
    const auto arguments = message.find("arguments");
    if (arguments == message.end() || !arguments->is_object())
    {
        return std::nullopt;
    }
    const auto trace = arguments->find("trace");
    if (trace == arguments->end() || !trace->is_string())
    {
        return std::nullopt;
    }
    return trace->get<std::string>();
}
} // namespace

dap_trace_recorder::~dap_trace_recorder()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (disposed_)
    {
        return;
    }

    write_trace_file_locked();
    disposed_ = true;
}

void dap_trace_recorder::record_input(const nlohmann::json &message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (disposed_)
    {
        return;
    }
    // Resolve tracing from the first launch/attach request before recording it,
    // so a non-tracing session frees its buffer and a tracing one keeps the
    // messages buffered before this point (e.g. initialize) for the flush.
    resolve_trace_path_once_locked(message);
    if (discarded_)
    {
        return;
    }
    record_locked("in", message);
}

void dap_trace_recorder::resolve_trace_path_once_locked(const nlohmann::json &message)
{
    if (resolved_ || !(is_request_with_command(message, "launch") || is_request_with_command(message, "attach")))
    {
        return;
    }

    resolved_ = true;

    const std::optional<std::string> trace_path = trace_path_from_request(message);
    if (trace_path.has_value() && !trace_path->empty())
    {
        try
        {
            path_ = prepare_trace_path(*trace_path);
            return; // Tracing enabled; buffered messages are kept and flushed on destruction.
        }
        catch (const std::exception &exception)
        {
            report_failure(exception.what());
        }
    }

    // No (valid) trace configured: stop recording and release the buffered transcript.
    discarded_ = true;
    messages_.clear();
    messages_.shrink_to_fit();
}

void dap_trace_recorder::record_output(const nlohmann::json &message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (disposed_ || discarded_)
    {
        return;
    }
    record_locked("out", message);
}

bool dap_trace_recorder::try_set_path(const std::filesystem::path &path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try
    {
        path_ = prepare_trace_path(path);
        return true;
    }
    catch (const std::exception &exception)
    {
        report_failure(exception.what());
        return false;
    }
}

void dap_trace_recorder::discard()
{
    std::lock_guard<std::mutex> lock(mutex_);
    discarded_ = true;
    messages_.clear();
    messages_.shrink_to_fit();
}

void dap_trace_recorder::record_locked(const char *direction, nlohmann::json message)
{
    // Merge an outbound console `output` event into the preceding one so a run
    // of console output collapses to a single entry.
    std::string output;
    if (!messages_.empty() && try_get_console_output(direction, message, output))
    {
        std::string previous_output;
        const trace_entry &last = messages_.back();
        if (try_get_console_output(last.direction.c_str(), last.message, previous_output))
        {
            nlohmann::json merged = message;
            merged["body"] = nlohmann::json{{"category", "console"}, {"output", previous_output + output}};
            messages_.back() = trace_entry{direction, std::move(merged)};
            return;
        }
    }

    messages_.push_back(trace_entry{direction, std::move(message)});
}

bool dap_trace_recorder::try_get_console_output(const char *direction, const nlohmann::json &message,
                                                std::string &output)
{
    output.clear();
    if (std::string_view(direction) != "out" || !message.is_object())
    {
        return false;
    }

    const auto type = message.find("type");
    if (type == message.end() || !type->is_string() || type->get<std::string>() != "event")
    {
        return false;
    }

    const auto event_name = message.find("event");
    if (event_name == message.end() || !event_name->is_string() || event_name->get<std::string>() != "output")
    {
        return false;
    }

    const auto body = message.find("body");
    if (body == message.end() || !body->is_object() || body->size() != 2)
    {
        return false;
    }

    const auto category = body->find("category");
    if (category == body->end() || !category->is_string() || category->get<std::string>() != "console")
    {
        return false;
    }

    const auto output_property = body->find("output");
    if (output_property == body->end() || !output_property->is_string())
    {
        return false;
    }

    output = output_property->get<std::string>();
    return true;
}

void dap_trace_recorder::write_trace_file_locked()
{
    if (!path_.has_value())
    {
        return;
    }

    try
    {
        nlohmann::json messages = nlohmann::json::array();
        for (const trace_entry &entry : messages_)
        {
            messages.push_back(nlohmann::json{{"direction", entry.direction}, {"message", entry.message}});
        }

        const nlohmann::json document{{"version", 1}, {"messages", std::move(messages)}};

        std::ofstream stream(*path_, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            report_failure(fmt::format("Unable to open trace path '{}' for writing.", path_->string()));
            return;
        }

        // Two-space indent for the trace output.
        stream << document.dump(2);
    }
    catch (const std::exception &exception)
    {
        report_failure(exception.what());
    }
}
} // namespace dap_dbgeng::transport
