# Generated DAP protocol, built as a static library whose precompiled header
# carries the protocol types to every consumer.
add_subdirectory(protocol)

# Production code lives in an internal OBJECT library so it can be compiled once
# and consumed by both the product executable and the test binary. The object
# library produces no standalone artifact: the only product target is
# `dap-dbgeng.exe`.
add_library(dap-dbgeng-objects OBJECT
  dap/dap_server.cpp
  debugger/debugger_session.cpp
  debugger/debugger_session_breakpoints.cpp
  debugger/debugger_session_command.cpp
  debugger/debugger_session_execution.cpp
  debugger/debugger_session_inspection.cpp
  debugger/debugger_session_memory.cpp
  debugger/debugger_session_symbols.cpp
  debugger/debugger_session_targets.cpp
  debugger/debugger_session_threads.cpp
  service/dap_server.cpp
  service/dap_server_session_events.cpp
  service/dap_server_attach.cpp
  service/dap_server_configuration_done.cpp
  service/dap_server_continue.cpp
  service/dap_server_data_breakpoint_info.cpp
  service/dap_server_disassemble.cpp
  service/dap_server_disconnect.cpp
  service/dap_server_evaluate.cpp
  service/dap_server_exception_info.cpp
  service/dap_server_initialize.cpp
  service/dap_server_launch.cpp
  service/dap_server_modules.cpp
  service/dap_server_next.cpp
  service/dap_server_pause.cpp
  service/dap_server_read_memory.cpp
  service/dap_server_scopes.cpp
  service/dap_server_set_breakpoints.cpp
  service/dap_server_set_data_breakpoints.cpp
  service/dap_server_set_exception_breakpoints.cpp
  service/dap_server_set_expression.cpp
  service/dap_server_set_function_breakpoints.cpp
  service/dap_server_set_instruction_breakpoints.cpp
  service/dap_server_set_variable.cpp
  service/dap_server_source.cpp
  service/dap_server_stack_trace.cpp
  service/dap_server_step_in.cpp
  service/dap_server_step_out.cpp
  service/dap_server_terminate.cpp
  service/dap_server_threads.cpp
  service/dap_server_variables.cpp
  service/dap_server_write_memory.cpp
  transport/dap_trace_recorder.cpp
  transport/io_stream_server.cpp
  util/dap_argument_reader.cpp
  util/dap_message_inspector.cpp
  util/debugger_session_dap_factory.cpp
  util/debugger_session_dispatcher.cpp
  util/windbg_command_classifier.cpp
)

target_include_directories(dap-dbgeng-objects
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Our hand-written code builds warning-clean at /W4 /WX. The generated protocol
# library and third-party headers are not held to this.
target_compile_options(dap-dbgeng-objects PRIVATE /W4 /WX)

target_precompile_headers(dap-dbgeng-objects
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h
)

target_link_libraries(dap-dbgeng-objects
  PUBLIC
    dap-dbgeng-protocol
    fmt::fmt
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# dbgeng / dbghelp ship with the Windows SDK (not vcpkg) and back the debug engine.
if(WIN32)
  target_link_libraries(dap-dbgeng-objects
    PUBLIC
      dbgeng
      dbghelp
  )
endif()

# The product executable: a thin entrypoint over the shared production code.
add_executable(dap-dbgeng
  main.cpp
)

target_precompile_headers(dap-dbgeng
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h
)

target_compile_options(dap-dbgeng PRIVATE /W4 /WX)

target_link_libraries(dap-dbgeng
  PRIVATE
    dap-dbgeng-objects
)
