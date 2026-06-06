#include "support/test_environment.h"

namespace dap_dbgeng::test_support
{
namespace fs = std::filesystem;

namespace
{
constexpr const char *kDefaultDbgEngPath = "C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbgeng.dll";
} // namespace

std::optional<std::string> env_var(const char *name)
{
    char *buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
    {
        return std::nullopt;
    }
    std::string value(buffer);
    free(buffer);
    if (value.empty())
    {
        return std::nullopt;
    }
    return value;
}

fs::path module_directory()
{
    wchar_t module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) > 0)
    {
        return fs::path(module_path).parent_path();
    }
    std::error_code ec;
    return fs::current_path(ec);
}

std::optional<fs::path> find_repository_root()
{
    std::error_code ec;

    std::vector<fs::path> starts;
    wchar_t module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) > 0)
    {
        starts.push_back(fs::path(module_path).parent_path());
    }
    starts.push_back(fs::current_path(ec));

    for (const auto &start : starts)
    {
        fs::path dir = start;
        for (int i = 0; i < 24 && !dir.empty(); ++i)
        {
            if (fs::is_directory(dir / "src", ec) && fs::exists(dir / "CMakePresets.json", ec))
            {
                return dir;
            }
            if (!dir.has_parent_path() || dir.parent_path() == dir)
            {
                break;
            }
            dir = dir.parent_path();
        }
    }
    return std::nullopt;
}

std::string resolve_dbgeng_path()
{
    if (auto configured = env_var("DAP_DBGENG_WINDBG_PATH"))
    {
        return fs::exists(*configured) ? *configured : std::string();
    }
    return fs::exists(kDefaultDbgEngPath) ? std::string(kDefaultDbgEngPath) : std::string();
}

std::string resolve_launch_target_path()
{
    if (auto configured = env_var("DAP_DBGENG_NATIVE_APP"))
    {
        return fs::exists(*configured) ? *configured : std::string();
    }
    // The native app builds into <binaryDir>/test-targets alongside the test
    // binary in <binaryDir>/tests, so resolve it relative to this executable.
    const fs::path sibling = module_directory().parent_path() / "test-targets" / "test_launch.exe";
    if (fs::exists(sibling))
    {
        return sibling.string();
    }
    if (auto root = find_repository_root())
    {
        const fs::path target = *root / "build" / "windows-x64" / "test-targets" / "test_launch.exe";
        if (fs::exists(target))
        {
            return target.string();
        }
    }
    return std::string();
}

std::string resolve_launch_target_directory()
{
    const std::string target = resolve_launch_target_path();
    return target.empty() ? std::string() : fs::path(target).parent_path().string();
}

std::string resolve_launch_target_source()
{
    if (auto root = find_repository_root())
    {
        const fs::path source = *root / "test-targets" / "testapp" / "main.cpp";
        if (fs::exists(source))
        {
            return source.string();
        }
    }
    return std::string();
}
} // namespace dap_dbgeng::test_support
