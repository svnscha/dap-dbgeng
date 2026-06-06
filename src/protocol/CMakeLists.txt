# Generated DAP protocol types and the string-dispatch service base. A static
# library so the large generated header compiles once and reaches consumers
# through a precompiled header rather than being re-parsed in every translation
# unit. dap_service.h transitively includes protocol.h.
add_library(dap-dbgeng-protocol STATIC protocol.cpp)

# src/ is the include root, so consumers resolve "protocol/...".
target_include_directories(dap-dbgeng-protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(dap-dbgeng-protocol
  PUBLIC
    nlohmann_json::nlohmann_json
)

# PUBLIC: linking targets get dap_service.h as a precompiled header, so they
# neither include it explicitly nor re-parse the generated protocol per TU.
target_precompile_headers(dap-dbgeng-protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_service.h
)
