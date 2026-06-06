# Native debuggee programs that the replay and integration tests launch or
# attach to. They build into ${CMAKE_BINARY_DIR}/test-targets so the tests
# always have a current binary (and matching PDB) to debug.

# Spawned under the debugger by launch-based fixtures; its source must stay
# stable so recorded sessions keep matching.
add_executable(test_launch main.cpp)

# A keepalive process that attach-based fixtures attach to.
add_executable(test_attach attach.cpp)

set_target_properties(test_launch test_attach PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-targets")

# These are debugging fixtures: the tests and recorded sessions assert on exact
# source lines and on locals being live. Optimization (the default in Release)
# rewrites line tables and elides locals, so always build them unoptimized with
# full debug info regardless of the tree's configuration.
if(MSVC)
  target_compile_options(test_launch PRIVATE /Od /Zi)
  target_compile_options(test_attach PRIVATE /Od /Zi)
  target_link_options(test_launch PRIVATE /DEBUG)
  target_link_options(test_attach PRIVATE /DEBUG)
endif()
