#include "debugger/debugger_session.h"

#include "debugger/debugger_session_internal.h"

namespace dap_dbgeng::debugger
{
// ---------------------------------------------------------------------------
// Symbols / source
// ---------------------------------------------------------------------------
void debugger_session::set_symbol_path(const std::vector<std::string> &symbol_paths)
{
    throw_if_disposed();
    std::string joined;
    for (std::size_t i = 0; i < symbol_paths.size(); ++i)
    {
        if (i != 0)
        {
            joined += ';';
        }
        joined += symbol_paths[i];
    }
    check_hr(symbols_->SetSymbolPath(joined.c_str()), "Could not set symbol path");
}

void debugger_session::set_source_path(const std::vector<std::string> &source_paths)
{
    throw_if_disposed();
    std::string joined;
    for (std::size_t i = 0; i < source_paths.size(); ++i)
    {
        if (i != 0)
        {
            joined += ';';
        }
        joined += source_paths[i];
    }
    execute_debugger_command(fmt::format(".srcpath {}", joined), "Could not set source path");
}

void debugger_session::enable_source_line_support()
{
    throw_if_disposed();
    execute_debugger_command(".lines -e", "Could not enable source line support");
    enable_source_line_stepping();
}

void debugger_session::enable_source_line_stepping()
{
    throw_if_disposed();
    execute_debugger_command("l+t", "Could not enable source-based stepping");
}

void debugger_session::reload_symbols(std::optional<std::string> module_name)
{
    throw_if_disposed();
    check_hr(symbols_->Reload(module_name ? module_name->c_str() : ""), "Could not reload symbols");
}
} // namespace dap_dbgeng::debugger
