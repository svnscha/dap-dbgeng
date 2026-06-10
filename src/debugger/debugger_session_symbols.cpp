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

std::vector<module_info> debugger_session::get_modules()
{
    throw_if_disposed();

    ULONG loaded = 0;
    ULONG unloaded = 0;
    check_hr(symbols_->GetNumberModules(&loaded, &unloaded), "Could not enumerate modules");

    std::vector<module_info> result;
    result.reserve(loaded);
    std::vector<char> image_name(kSymbolBufferBytes);
    std::vector<char> module_name(kRegisterNameBufferBytes);
    for (ULONG index = 0; index < loaded; ++index)
    {
        DEBUG_MODULE_PARAMETERS params{};
        if (FAILED(symbols_->GetModuleParameters(1, nullptr, index, &params)))
        {
            continue;
        }

        ULONG image_size = 0;
        ULONG module_size = 0;
        if (FAILED(symbols_->GetModuleNames(index, 0, image_name.data(), static_cast<ULONG>(image_name.size()),
                                            &image_size, module_name.data(), static_cast<ULONG>(module_name.size()),
                                            &module_size, nullptr, 0, nullptr)))
        {
            continue;
        }

        module_info info;
        info.name = buffer_to_trimmed_string(module_name, module_size);
        info.image_path = buffer_to_trimmed_string(image_name, image_size);
        info.base = params.Base;
        info.size = params.Size;
        switch (params.SymbolType)
        {
        case DEBUG_SYMTYPE_NONE:
            info.symbol_status = "Symbols not found";
            break;
        case DEBUG_SYMTYPE_DEFERRED:
            info.symbol_status = "Symbols not loaded";
            break;
        case DEBUG_SYMTYPE_EXPORT:
            info.symbol_status = "Exports only";
            break;
        default:
            info.symbol_status = "Symbols loaded";
            break;
        }
        result.push_back(std::move(info));
    }
    return result;
}
} // namespace dap_dbgeng::debugger
