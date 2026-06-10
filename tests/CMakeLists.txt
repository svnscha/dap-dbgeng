# Test binary: links the shared production OBJECT library plus GoogleTest.
add_executable(dap-dbgeng-tests
  core/channel_tests.cpp
  dap/dap_server_tests.cpp
  debugger/integration/debugger_session_tests.cpp
  protocol/protocol_tests.cpp
  replay/replay_harness.cpp
  replay/replay_tests.cpp
  service/dap_server_tests.cpp
  service/dap_server_handler_tests.cpp
  service/dap_server_live_tests.cpp
  support/test_environment.cpp
  transport/dap_trace_recorder_tests.cpp
  util/base64_tests.cpp
  util/dap_argument_reader_tests.cpp
  util/debugger_session_dap_factory_tests.cpp
)

target_precompile_headers(dap-dbgeng-tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/stdafx.h
)

# Test code is held to the same /W4 /WX bar as production (GoogleTest aside).
target_compile_options(dap-dbgeng-tests PRIVATE /W4 /WX)

# The replay harness includes its own headers relative to the tests/ root
# (e.g. "replay/replay_harness.h"); the production include path only carries src/.
target_include_directories(dap-dbgeng-tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# The replay tests resolve recorded fixtures relative to this compile-time path
# instead of a runtime working-directory copy.
target_compile_definitions(dap-dbgeng-tests
  PRIVATE
    DAP_REPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replay/data"
)

target_link_libraries(dap-dbgeng-tests
  PRIVATE
    dap-dbgeng-objects
    GTest::gtest
    GTest::gtest_main
)

if(MSVC)
  set_property(TARGET dap-dbgeng-tests PROPERTY VS_GLOBAL_UseMultiToolTask false)
endif()

# The replay tests spawn the built product executable out-of-process, so make
# sure it is built alongside the test binary.
add_dependencies(dap-dbgeng-tests dap-dbgeng)

# Unit/integration tests (everything except the out-of-process replay suite).
add_test(NAME dap-dbgeng-tests COMMAND dap-dbgeng-tests --gtest_filter=-Replay.*)

# The out-of-process replay suite as its own entry, so `ctest -R Replay`
# (npm run test:replay) drives just the recorded sessions and the heavy live
# tests run once per `ctest`, not twice.
add_test(NAME Replay COMMAND dap-dbgeng-tests --gtest_filter=Replay.*)
