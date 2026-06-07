#include "replay/replay_harness.h"

#include "dap/dap_server.h" // dap_dbgeng::frame_message
#include "support/test_environment.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <tlhelp32.h>

#pragma comment(lib, "ws2_32.lib")

namespace dap_dbgeng::replay
{
namespace fs = std::filesystem;

namespace
{
constexpr const char *kDefaultDbgEngPath = "C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbgeng.dll";
constexpr int kDefaultReplayAttempts = 2;

using test_support::env_var;

// JSON-escaped inner text of a string value: serialize the string, then strip
// the surrounding quotes so a token sitting inside a JSON string is replaced
// with a valid escaped value (Windows path backslashes doubled).
std::string json_inner(const std::string &value)
{
    const std::string serialized = nlohmann::json(value).dump();
    // serialized is `"..."`; drop the surrounding quotes.
    return serialized.substr(1, serialized.size() - 2);
}

// Replace every (non-overlapping) occurrence of `token` in `text` with `value`.
void replace_all(std::string &text, const std::string &token, const std::string &value)
{
    if (token.empty())
    {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
}

using test_support::find_repository_root;

// Find a free loopback TCP port by binding to port 0 and reading the assigned
// port.
int get_free_tcp_port()
{
    WSADATA wsa_data{};
    const bool wsa_started = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;

    int port = 0;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener != INVALID_SOCKET)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
        {
            sockaddr_in bound{};
            int len = sizeof(bound);
            if (getsockname(listener, reinterpret_cast<sockaddr *>(&bound), &len) == 0)
            {
                port = ntohs(bound.sin_port);
            }
        }
        closesocket(listener);
    }

    if (wsa_started)
    {
        WSACleanup();
    }
    return port;
}

// Kill every running process whose image name (without extension) matches
// `process_name`. Best effort.
void kill_processes_by_name(const std::string &process_name)
{
    if (process_name.empty())
    {
        return;
    }

    const std::string target_lower = [&] {
        std::string lowered = process_name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            const std::wstring wname = entry.szExeFile;
            std::string name;
            name.reserve(wname.size());
            for (const wchar_t wc : wname)
            {
                name.push_back(static_cast<char>(wc));
            }
            // Strip a trailing extension for comparison.
            const std::size_t dot = name.find_last_of('.');
            std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
            std::transform(stem.begin(), stem.end(), stem.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (stem == target_lower)
            {
                HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (process != nullptr)
                {
                    TerminateProcess(process, 1);
                    CloseHandle(process);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
}

std::string file_stem(const fs::path &path)
{
    return path.stem().string();
}

// ---------------------------------------------------------------------------
// A spawned helper process (attach target / dbgsrv) terminated on destruction.
// ---------------------------------------------------------------------------
class spawned_process
{
  public:
    spawned_process() = default;
    spawned_process(HANDLE process, HANDLE job, DWORD pid) : process_(process), job_(job), pid_(pid)
    {
    }
    spawned_process(const spawned_process &) = delete;
    spawned_process &operator=(const spawned_process &) = delete;
    spawned_process(spawned_process &&other) noexcept
        : process_(std::exchange(other.process_, nullptr)), job_(std::exchange(other.job_, nullptr)),
          pid_(std::exchange(other.pid_, 0))
    {
    }
    spawned_process &operator=(spawned_process &&other) noexcept
    {
        if (this != &other)
        {
            kill();
            process_ = std::exchange(other.process_, nullptr);
            job_ = std::exchange(other.job_, nullptr);
            pid_ = std::exchange(other.pid_, 0);
        }
        return *this;
    }
    ~spawned_process()
    {
        kill();
    }

    DWORD pid() const
    {
        return pid_;
    }

    void kill()
    {
        if (process_ != nullptr)
        {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, 1000);
            CloseHandle(process_);
            process_ = nullptr;
        }
        // Closing the kill-on-close job terminates the whole process tree, so a
        // server that forked children (e.g. dbgsrv) leaves nothing behind.
        if (job_ != nullptr)
        {
            CloseHandle(job_);
            job_ = nullptr;
        }
        pid_ = 0;
    }

  private:
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    DWORD pid_ = 0;
};

// Start a plain (no-redirect) process inside a kill-on-close job so it and any
// children are terminated when the returned handle is destroyed.
spawned_process start_process(const fs::path &exe, const std::wstring &arguments)
{
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    if (!arguments.empty())
    {
        command += L" " + arguments;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring working_dir = exe.parent_path().wstring();
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                                   working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
    if (!ok)
    {
        if (job != nullptr)
        {
            CloseHandle(job);
        }
        throw replay_skip(fmt::format("Could not start process '{}' (error {}).", exe.string(), GetLastError()));
    }

    if (job != nullptr)
    {
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return spawned_process(pi.hProcess, job, pi.dwProcessId);
}

// ---------------------------------------------------------------------------
// Out-of-process DAP adapter over redirected stdio (ports
// OutOfProcessDapAdapter). Spawns the adapter, frames outbound DAP to its
// stdin, and parses inbound frames on a background reader thread.
// ---------------------------------------------------------------------------
class out_of_process_adapter
{
  public:
    explicit out_of_process_adapter(const fs::path &adapter_path)
    {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE child_stdin_read = nullptr;
        HANDLE child_stdout_write = nullptr;

        if (!CreatePipe(&child_stdin_read, &parent_stdin_write_, &sa, 0) ||
            !CreatePipe(&parent_stdout_read_, &child_stdout_write, &sa, 0))
        {
            throw replay_skip("Could not create adapter stdio pipes.");
        }

        // The parent ends must NOT be inherited by the child.
        SetHandleInformation(parent_stdin_write_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(parent_stdout_read_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = child_stdin_read;
        si.hStdOutput = child_stdout_write;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + adapter_path.wstring() + L"\"";
        std::wstring working_dir = adapter_path.parent_path().wstring();
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');

        const BOOL ok = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                       working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);

        // Close the child ends in the parent regardless of success.
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdout_write);

        if (!ok)
        {
            CloseHandle(parent_stdin_write_);
            CloseHandle(parent_stdout_read_);
            parent_stdin_write_ = nullptr;
            parent_stdout_read_ = nullptr;
            throw replay_skip(
                fmt::format("Could not start adapter '{}' (error {}).", adapter_path.string(), GetLastError()));
        }

        process_ = pi.hProcess;
        CloseHandle(pi.hThread);

        reader_ = std::thread([this] { read_loop(); });
    }

    out_of_process_adapter(const out_of_process_adapter &) = delete;
    out_of_process_adapter &operator=(const out_of_process_adapter &) = delete;

    ~out_of_process_adapter()
    {
        // Close child stdin so the adapter sees EOF and exits.
        if (parent_stdin_write_ != nullptr)
        {
            CloseHandle(parent_stdin_write_);
            parent_stdin_write_ = nullptr;
        }

        if (process_ != nullptr)
        {
            if (WaitForSingleObject(process_, 5000) != WAIT_OBJECT_0)
            {
                TerminateProcess(process_, 1);
                WaitForSingleObject(process_, 1000);
            }
        }

        // The reader exits when stdout is closed (which happens once the child
        // exits). Close our read end to unblock a stuck read, then join.
        if (reader_.joinable())
        {
            reader_.join();
        }

        if (parent_stdout_read_ != nullptr)
        {
            CloseHandle(parent_stdout_read_);
            parent_stdout_read_ = nullptr;
        }
        if (process_ != nullptr)
        {
            CloseHandle(process_);
            process_ = nullptr;
        }
    }

    void send(const nlohmann::json &request)
    {
        const std::string framed = dap_dbgeng::frame_message(request);
        std::size_t total = 0;
        while (total < framed.size())
        {
            DWORD written = 0;
            if (!WriteFile(parent_stdin_write_, framed.data() + total, static_cast<DWORD>(framed.size() - total),
                           &written, nullptr) ||
                written == 0)
            {
                throw replay_assertion_error("Failed to write a DAP frame to the adapter stdin.");
            }
            total += written;
        }
    }

    // Poll until at least `count` non-output messages have arrived, or the read
    // loop ends, or the timeout elapses. Returns a copy of the count-th
    // non-output message (1-based). Throws replay_assertion_error on timeout.
    nlohmann::json wait_for_non_output(std::size_t count, int timeout_milliseconds)
    {
        const auto started = std::chrono::steady_clock::now();
        while (
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() <
            timeout_milliseconds)
        {
            {
                std::lock_guard<std::mutex> lock(gate_);
                if (non_output_.size() >= count)
                {
                    return non_output_[count - 1];
                }
            }

            if (reader_done_.load())
            {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        throw replay_assertion_error(
            fmt::format("Timed out waiting for {} non-output DAP messages from the adapter.", count));
    }

    std::vector<nlohmann::json> snapshot_all()
    {
        std::lock_guard<std::mutex> lock(gate_);
        return all_;
    }

    std::vector<nlohmann::json> snapshot_non_output()
    {
        std::lock_guard<std::mutex> lock(gate_);
        return non_output_;
    }

  private:
    static bool is_output_event(const nlohmann::json &message)
    {
        return message.is_object() && message.value("type", std::string{}) == "event" &&
               message.value("event", std::string{}) == "output";
    }

    bool read_exact(char *buffer, std::size_t length)
    {
        std::size_t total = 0;
        while (total < length)
        {
            DWORD read = 0;
            if (!ReadFile(parent_stdout_read_, buffer + total, static_cast<DWORD>(length - total), &read, nullptr) ||
                read == 0)
            {
                return false;
            }
            total += read;
        }
        return true;
    }

    // Read one DAP frame; returns false on EOF / error.
    bool read_frame(std::string &body_out)
    {
        std::string header;
        char ch = 0;
        while (!(header.size() >= 4 && header[header.size() - 4] == '\r' && header[header.size() - 3] == '\n' &&
                 header[header.size() - 2] == '\r' && header[header.size() - 1] == '\n'))
        {
            DWORD read = 0;
            if (!ReadFile(parent_stdout_read_, &ch, 1, &read, nullptr) || read == 0)
            {
                return false;
            }
            header.push_back(ch);
        }

        const long long content_length = parse_content_length(header);
        if (content_length < 0)
        {
            return false;
        }
        if (content_length == 0)
        {
            body_out.clear();
            return true;
        }

        body_out.assign(static_cast<std::size_t>(content_length), '\0');
        return read_exact(body_out.data(), static_cast<std::size_t>(content_length));
    }

    static long long parse_content_length(const std::string &header)
    {
        constexpr std::string_view prefix = "content-length:";
        std::size_t line_start = 0;
        while (line_start < header.size())
        {
            std::size_t line_end = header.find("\r\n", line_start);
            if (line_end == std::string::npos)
            {
                line_end = header.size();
            }
            std::string_view line(header.data() + line_start, line_end - line_start);
            if (line.size() >= prefix.size())
            {
                std::string lowered(line.substr(0, prefix.size()));
                std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lowered == prefix)
                {
                    std::string_view value = line.substr(prefix.size());
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
                        return std::stoll(std::string(value));
                    }
                    catch (const std::exception &)
                    {
                        return -1;
                    }
                }
            }
            if (line_end == header.size())
            {
                break;
            }
            line_start = line_end + 2;
        }
        return -1;
    }

    void read_loop()
    {
        try
        {
            std::string body;
            while (read_frame(body))
            {
                nlohmann::json json;
                try
                {
                    json = nlohmann::json::parse(body);
                }
                catch (const std::exception &)
                {
                    continue; // Skip unparseable frames.
                }

                std::lock_guard<std::mutex> lock(gate_);
                all_.push_back(json);
                if (!is_output_event(json))
                {
                    non_output_.push_back(std::move(json));
                }
            }
        }
        catch (...)
        {
            // The adapter exited or the pipe closed; the replay loop surfaces
            // any resulting timeout.
        }
        reader_done_.store(true);
    }

    HANDLE process_ = nullptr;
    HANDLE parent_stdin_write_ = nullptr;
    HANDLE parent_stdout_read_ = nullptr;
    std::thread reader_;
    std::atomic<bool> reader_done_{false};
    std::mutex gate_;
    std::vector<nlohmann::json> all_;
    std::vector<nlohmann::json> non_output_;
};

// ---------------------------------------------------------------------------
// Volatile-id rewrite state. Recorded (expected) ids map to the actual ids the
// live adapter assigned.
// ---------------------------------------------------------------------------
class replay_state
{
  public:
    nlohmann::json rewrite_volatile_ids(const nlohmann::json &request) const
    {
        nlohmann::json node = request;
        if (!node.is_object())
        {
            throw replay_assertion_error("Recorded request must be a JSON object.");
        }

        auto args = node.find("arguments");
        if (args != node.end() && args->is_object())
        {
            // The recorded "trace" path is a self-referential capture artifact;
            // drop it so the replayed adapter does not start recording over the
            // original capture.
            args->erase("trace");

            rewrite_int(*args, "threadId", thread_ids_);
            rewrite_int(*args, "frameId", frame_ids_);
            rewrite_int(*args, "variablesReference", variables_references_);
            rewrite_string(*args, "memoryReference", memory_references_);
        }

        return node;
    }

    void record_volatile_ids(const nlohmann::json &expected, const nlohmann::json &actual)
    {
        map_thread_id(expected, actual);
        map_array_ids(expected, actual, "threads", "id", thread_ids_);
        map_array_ids(expected, actual, "stackFrames", "id", frame_ids_);
        map_array_ids(expected, actual, "scopes", "variablesReference", variables_references_);
        map_array_ids(expected, actual, "variables", "variablesReference", variables_references_);
        map_array_strings(expected, actual, "stackFrames", "instructionPointerReference", memory_references_);
        map_array_strings(expected, actual, "instructions", "address", memory_references_);
    }

  private:
    static void rewrite_int(nlohmann::json &arguments, const char *property, const std::map<long long, long long> &map)
    {
        auto it = arguments.find(property);
        if (it == arguments.end() || !it->is_number_integer())
        {
            return;
        }
        const long long recorded = it->get<long long>();
        auto mapped = map.find(recorded);
        if (mapped != map.end())
        {
            *it = mapped->second;
        }
    }

    static void rewrite_string(nlohmann::json &arguments, const char *property,
                               const std::map<std::string, std::string> &map)
    {
        auto it = arguments.find(property);
        if (it == arguments.end() || !it->is_string())
        {
            return;
        }
        const std::string recorded = it->get<std::string>();
        if (recorded.empty())
        {
            return;
        }
        // Case-insensitive lookup.
        for (const auto &[key, value] : map)
        {
            if (equals_ignore_case(key, recorded))
            {
                *it = value;
                return;
            }
        }
    }

    static bool equals_ignore_case(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            {
                return false;
            }
        }
        return true;
    }

    static const nlohmann::json *body_property(const nlohmann::json &message, const char *property)
    {
        if (!message.is_object())
        {
            return nullptr;
        }
        auto body = message.find("body");
        if (body == message.end() || !body->is_object())
        {
            return nullptr;
        }
        auto prop = body->find(property);
        if (prop == body->end())
        {
            return nullptr;
        }
        return &(*prop);
    }

    void map_thread_id(const nlohmann::json &expected, const nlohmann::json &actual)
    {
        const nlohmann::json *e = body_property(expected, "threadId");
        const nlohmann::json *a = body_property(actual, "threadId");
        if (e != nullptr && a != nullptr && e->is_number_integer() && a->is_number_integer())
        {
            thread_ids_[e->get<long long>()] = a->get<long long>();
        }
    }

    static void map_array_ids(const nlohmann::json &expected, const nlohmann::json &actual, const char *array_name,
                              const char *id_property, std::map<long long, long long> &map)
    {
        const nlohmann::json *e = body_property(expected, array_name);
        const nlohmann::json *a = body_property(actual, array_name);
        if (e == nullptr || a == nullptr || !e->is_array() || !a->is_array())
        {
            return;
        }
        const std::size_t count = std::min(e->size(), a->size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &ei = (*e)[i];
            const auto &ai = (*a)[i];
            if (!ei.is_object() || !ai.is_object())
            {
                continue;
            }
            auto eid = ei.find(id_property);
            auto aid = ai.find(id_property);
            if (eid != ei.end() && aid != ai.end() && eid->is_number_integer() && aid->is_number_integer())
            {
                map[eid->get<long long>()] = aid->get<long long>();
            }
        }
    }

    static void map_array_strings(const nlohmann::json &expected, const nlohmann::json &actual, const char *array_name,
                                  const char *property, std::map<std::string, std::string> &map)
    {
        const nlohmann::json *e = body_property(expected, array_name);
        const nlohmann::json *a = body_property(actual, array_name);
        if (e == nullptr || a == nullptr || !e->is_array() || !a->is_array())
        {
            return;
        }
        const std::size_t count = std::min(e->size(), a->size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &ei = (*e)[i];
            const auto &ai = (*a)[i];
            if (!ei.is_object() || !ai.is_object())
            {
                continue;
            }
            auto ev = ei.find(property);
            auto av = ai.find(property);
            if (ev != ei.end() && av != ai.end() && ev->is_string() && av->is_string())
            {
                const std::string es = ev->get<std::string>();
                const std::string as = av->get<std::string>();
                if (!es.empty() && !as.empty())
                {
                    map[es] = as;
                }
            }
        }
    }

    std::map<long long, long long> thread_ids_;
    std::map<long long, long long> frame_ids_;
    std::map<long long, long long> variables_references_;
    std::map<std::string, std::string> memory_references_;
};

bool is_output_event(const nlohmann::json &message)
{
    return message.is_object() && message.value("type", std::string{}) == "event" &&
           message.value("event", std::string{}) == "output";
}

// Compares ONLY type + (command,success) / (event). Throws
// replay_assertion_error on mismatch.
void assert_recorded_message_matches(const nlohmann::json &expected, const nlohmann::json &actual, std::size_t index)
{
    const std::string expected_type = expected.value("type", std::string{});
    const std::string actual_type = actual.value("type", std::string{});
    if (expected_type != actual_type)
    {
        throw replay_assertion_error(
            fmt::format("Unexpected message type at non-output index {} (expected '{}', got '{}').", index,
                        expected_type, actual_type));
    }

    if (expected_type == "response")
    {
        const std::string ec = expected.value("command", std::string{});
        const std::string ac = actual.value("command", std::string{});
        if (ec != ac)
        {
            throw replay_assertion_error(fmt::format(
                "Unexpected response command at non-output index {} (expected '{}', got '{}').", index, ec, ac));
        }
        const bool es = expected.value("success", false);
        const bool as = actual.value("success", false);
        if (es != as)
        {
            throw replay_assertion_error(fmt::format(
                "Unexpected response success at non-output index {} (expected {}, got {}).", index, es, as));
        }
        return;
    }

    if (expected_type == "event")
    {
        const std::string ee = expected.value("event", std::string{});
        const std::string ae = actual.value("event", std::string{});
        if (ee != ae)
        {
            throw replay_assertion_error(
                fmt::format("Unexpected event at non-output index {} (expected '{}', got '{}').", index, ee, ae));
        }
    }
}

bool is_request(const nlohmann::json &message)
{
    return message.is_object() && message.value("type", std::string{}) == "request";
}

// First inbound launch request's arguments.target, if present.
std::optional<std::string> recorded_launch_target(const recorded_session &session)
{
    for (const auto &entry : session.messages)
    {
        if (entry.direction != "in" || !is_request(entry.message))
        {
            continue;
        }
        if (entry.message.value("command", std::string{}) != "launch")
        {
            continue;
        }
        auto args = entry.message.find("arguments");
        if (args == entry.message.end() || !args->is_object())
        {
            return std::nullopt;
        }
        auto target = args->find("program");
        if (target != args->end() && target->is_string())
        {
            return target->get<std::string>();
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void assert_launch_target_exists_or_skip(const recorded_session &session)
{
    auto target = recorded_launch_target(session);
    if (target && !target->empty() && !fs::exists(*target))
    {
        throw replay_skip(fmt::format(
            "The launch target '{}' referenced by the recorded session does not exist. Build the native test app.",
            *target));
    }
}

void kill_recorded_session_processes(const recorded_session &session)
{
    auto target = recorded_launch_target(session);
    if (target && !target->empty())
    {
        kill_processes_by_name(file_stem(fs::path(*target)));
    }
}

// One replay attempt against a fresh adapter.
replay_result replay_once(const recorded_session &session, int timeout_milliseconds)
{
    assert_launch_target_exists_or_skip(session);
    kill_recorded_session_processes(session);

    const fs::path adapter = adapter_path_or_skip();

    struct session_killer
    {
        const recorded_session &s;
        ~session_killer()
        {
            kill_recorded_session_processes(s);
        }
    } killer{session};

    out_of_process_adapter adapter_proc(adapter);
    replay_state state;

    // Non-output matching tolerates one thing only: the float of `thread`
    // lifecycle events relative to their neighbours. DAP imposes no ordering
    // between a thread started/exited event and the stop/continue of a
    // *different* thread, and dbgeng delivers the final-step ExitThread vs the
    // break state-change in a nondeterministic order — so a recording can put a
    // `thread` event just before or after an adjacent `stopped` event and both
    // are valid. We therefore require every NON-thread message (responses + all
    // non-thread events) to match in strict recorded order, while `thread`
    // events are paired FIFO wherever they land. The full thread-event multiset
    // must still appear (extra or missing thread events fail), so this does not
    // mask a real ordering bug — only the thread/non-thread interleaving floats.
    auto is_thread_event = [](const nlohmann::json &m) {
        return m.is_object() && m.value("type", std::string{}) == "event" &&
               m.value("event", std::string{}) == "thread";
    };

    std::size_t actual_cursor = 0; // 1-based count of actual non-output messages consumed (in arrival order)
    std::size_t diagnostic_index = 0;
    std::deque<nlohmann::json> pending_expected_threads; // recorded thread events awaiting their actual
    std::deque<nlohmann::json> pending_actual_threads;   // actual thread events awaiting their recorded peer

    auto pull_actual = [&]() -> nlohmann::json {
        ++actual_cursor;
        return adapter_proc.wait_for_non_output(actual_cursor, timeout_milliseconds);
    };

    for (const auto &entry : session.messages)
    {
        if (entry.direction == "in")
        {
            adapter_proc.send(state.rewrite_volatile_ids(entry.message));
            continue;
        }

        if (entry.direction != "out" || is_output_event(entry.message))
        {
            continue;
        }

        const nlohmann::json &expected = entry.message;
        const std::size_t index = diagnostic_index++;

        if (is_thread_event(expected))
        {
            // Defer: pair with an already-seen actual thread event, else wait for one to float in later.
            if (!pending_actual_threads.empty())
            {
                state.record_volatile_ids(expected, pending_actual_threads.front());
                pending_actual_threads.pop_front();
            }
            else
            {
                pending_expected_threads.push_back(expected);
            }
            continue;
        }

        // Consume actual messages until the next NON-thread one, which must match this expected.
        // Any thread events seen along the way are paired with deferred recorded thread events (or stashed).
        while (true)
        {
            const nlohmann::json actual = pull_actual();
            if (is_thread_event(actual))
            {
                if (!pending_expected_threads.empty())
                {
                    state.record_volatile_ids(pending_expected_threads.front(), actual);
                    pending_expected_threads.pop_front();
                }
                else
                {
                    pending_actual_threads.push_back(actual);
                }
                continue;
            }

            assert_recorded_message_matches(expected, actual, index);
            state.record_volatile_ids(expected, actual);
            break;
        }
    }

    // Drain recorded thread events whose actual counterpart floats in after the last non-thread message.
    while (!pending_expected_threads.empty())
    {
        const nlohmann::json actual = pull_actual(); // a genuinely missing thread event surfaces as a timeout
        if (is_thread_event(actual))
        {
            state.record_volatile_ids(pending_expected_threads.front(), actual);
            pending_expected_threads.pop_front();
        }
        // Trailing non-thread actuals are ignored: only the recorded non-output count is
        // consumed and anything after is ignored.
    }

    // Surplus actual thread events are tolerated: how many threads a process (and
    // the loader/CRT) spins up is environment-dependent, so a different OS build
    // — e.g. a CI runner vs. the machine a fixture was recorded on — can emit one
    // or more extra thread started/exited events. The recording's full set must
    // still appear (a *missing* recorded thread event surfaces as a timeout in the
    // drain loop above) and every non-thread message is still matched in strict
    // order, so this only loosens the inherently nondeterministic thread lifecycle.
    if (!pending_actual_threads.empty())
    {
        spdlog::debug("Replay tolerated {} extra thread event(s) with no recorded counterpart.",
                      pending_actual_threads.size());
    }

    replay_result result;
    result.all = adapter_proc.snapshot_all();
    result.non_output = adapter_proc.snapshot_non_output();
    return result;
}

// One full attempt including helper-process setup.
replay_result replay_file_once(const std::string &file_name, int timeout_milliseconds)
{
    std::map<std::string, std::string> substitutions;
    substitutions["${workspaceFolder}"] = json_inner(repository_root_or_skip().string());
    substitutions["${dbgEngPath}"] = json_inner(dbgeng_path_or_skip());

    std::optional<spawned_process> process_server;
    std::optional<spawned_process> attach_target;

    if (requires_process_server(file_name))
    {
        const int port = get_free_tcp_port();
        if (port == 0)
        {
            throw replay_skip("Could not find a free TCP port for the process server.");
        }
        process_server = start_process(process_server_path_or_skip(), L"-t tcp:port=" + std::to_wstring(port));
        // Give the process server a moment to begin listening.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        substitutions["${dbgsrvPort}"] = std::to_string(port);
    }

    if (requires_attach_target(file_name))
    {
        attach_target = start_process(attach_target_path_or_skip(), L"");
        // Give the target a moment to be running before the replay attaches.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        substitutions["${attachProcessId}"] = std::to_string(attach_target->pid());
    }

    const recorded_session session = load_session(file_name, substitutions);
    return replay_once(session, timeout_milliseconds);
    // process_server / attach_target are torn down by their destructors.
}

} // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

std::filesystem::path repository_root_or_skip()
{
    auto root = find_repository_root();
    if (!root)
    {
        throw replay_skip("Could not locate the repository root (an ancestor containing 'src' and CMakePresets.json).");
    }
    return *root;
}

std::string dbgeng_path_or_skip()
{
    if (auto configured = env_var("DAP_DBGENG_WINDBG_PATH"))
    {
        if (fs::exists(*configured))
        {
            return *configured;
        }
        throw replay_skip(fmt::format("DAP_DBGENG_WINDBG_PATH does not exist: '{}'.", *configured));
    }
    if (fs::exists(kDefaultDbgEngPath))
    {
        return kDefaultDbgEngPath;
    }
    throw replay_skip(fmt::format("Set DAP_DBGENG_WINDBG_PATH or install dbgeng at '{}'.", kDefaultDbgEngPath));
}

std::filesystem::path adapter_path_or_skip()
{
    if (auto configured = env_var("DAP_DBGENG_EXE"))
    {
        if (fs::exists(*configured))
        {
            return *configured;
        }
        throw replay_skip(fmt::format("DAP_DBGENG_EXE does not exist: '{}'.", *configured));
    }
    // The adapter builds into <binaryDir>/src alongside the test binary in
    // <binaryDir>/tests, so resolve it relative to this executable (works for
    // any build tree: Debug, Release, ...).
    const fs::path sibling = test_support::module_directory().parent_path() / "src" / "dap-dbgeng.exe";
    if (fs::exists(sibling))
    {
        return sibling;
    }
    const fs::path adapter = repository_root_or_skip() / "build" / "windows-x64" / "src" / "dap-dbgeng.exe";
    if (fs::exists(adapter))
    {
        return adapter;
    }
    throw replay_skip(
        fmt::format("The adapter executable was not found at '{}'. Build the C++ adapter or set DAP_DBGENG_EXE.",
                    adapter.string()));
}

std::filesystem::path launch_target_path_or_skip()
{
    if (auto configured = env_var("DAP_DBGENG_NATIVE_APP"))
    {
        if (fs::exists(*configured))
        {
            return *configured;
        }
        throw replay_skip(fmt::format("DAP_DBGENG_NATIVE_APP does not exist: '{}'.", *configured));
    }
    const fs::path sibling = test_support::module_directory().parent_path() / "test-targets" / "test_launch.exe";
    if (fs::exists(sibling))
    {
        return sibling;
    }
    const fs::path target = repository_root_or_skip() / "build" / "windows-x64" / "test-targets" / "test_launch.exe";
    if (fs::exists(target))
    {
        return target;
    }
    throw replay_skip(
        fmt::format("Build the native test app or set DAP_DBGENG_NATIVE_APP. Expected: '{}'.", target.string()));
}

std::filesystem::path attach_target_path_or_skip()
{
    const fs::path launch = launch_target_path_or_skip();
    const fs::path attach = launch.parent_path() / "test_attach.exe";
    if (fs::exists(attach))
    {
        return attach;
    }
    throw replay_skip(
        fmt::format("Build the native test apps or place 'test_attach.exe' next to the launch target. Expected: '{}'.",
                    attach.string()));
}

std::filesystem::path process_server_path_or_skip()
{
    const std::string dbgeng = dbgeng_path_or_skip();
    const fs::path directory = fs::is_directory(dbgeng) ? fs::path(dbgeng) : fs::path(dbgeng).parent_path();
    const fs::path dbgsrv = directory.empty() ? fs::path("dbgsrv.exe") : (directory / "dbgsrv.exe");
    if (fs::exists(dbgsrv))
    {
        return dbgsrv;
    }
    throw replay_skip(
        fmt::format("dbgsrv.exe is required for remote process-server replays. Expected next to dbgeng.dll at '{}'.",
                    dbgsrv.string()));
}

std::filesystem::path fixture_path(const std::string &file_name)
{
    return fs::path(DAP_REPLAY_FIXTURE_DIR) / file_name;
}

std::string read_fixture_text(const std::string &file_name)
{
    const fs::path path = fixture_path(file_name);
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw replay_skip(fmt::format("Could not open fixture '{}'.", path.string()));
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool requires_attach_target(const std::string &file_name)
{
    return read_fixture_text(file_name).find("${attachProcessId}") != std::string::npos;
}

bool requires_process_server(const std::string &file_name)
{
    return read_fixture_text(file_name).find("${dbgsrvPort}") != std::string::npos;
}

recorded_session load_session(const std::string &file_name, const std::map<std::string, std::string> &substitutions)
{
    std::string text = read_fixture_text(file_name);
    for (const auto &[token, value] : substitutions)
    {
        replace_all(text, token, value);
    }

    nlohmann::json document = nlohmann::json::parse(text);

    recorded_session session;
    const auto &messages = document.at("messages");
    for (const auto &entry : messages)
    {
        recorded_message message;
        message.direction = entry.value("direction", std::string{});
        message.message = entry.at("message");
        session.messages.push_back(std::move(message));
    }
    return session;
}

replay_result replay(const std::string &file_name, int timeout_milliseconds)
{
    std::optional<replay_assertion_error> last_failure;
    for (int attempt = 1; attempt <= kDefaultReplayAttempts; ++attempt)
    {
        try
        {
            return replay_file_once(file_name, timeout_milliseconds);
        }
        catch (const replay_assertion_error &error)
        {
            if (attempt < kDefaultReplayAttempts)
            {
                last_failure = error;
                continue;
            }
            throw;
        }
    }
    if (last_failure)
    {
        throw *last_failure;
    }
    throw replay_assertion_error("Recorded DAP session replay failed.");
}

} // namespace dap_dbgeng::replay
