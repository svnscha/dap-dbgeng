cmake_minimum_required(VERSION 3.21)

project(dap-dbgeng LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "dap-dbgeng is Windows-only: it requires dbgeng.dll / dbghelp.dll and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The Release package uses a static vcpkg triplet (x64-windows-static) so the adapter
# ships as a single self-contained exe (no fmt/spdlog DLLs to bundle). Link the MSVC
# runtime statically to match those static dependencies. The Debug tree keeps the
# dynamic triplet and CRT for faster, lighter iteration.
if(VCPKG_TARGET_TRIPLET MATCHES "static")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

# Native debuggee programs (test_launch / test_attach) the tests drive. Built
# with the tree so the replay/integration tests always have a current target.
add_subdirectory(test-targets/testapp)

# The WDK kernel test driver is a separate VS+WDK build (it cannot build under
# Ninja). It is off by default and unused by the automated tests; enable it with
# the Visual Studio generator, or use `npm run build:sys`. See test-targets/sys.
option(DAP_DBGENG_BUILD_KERNEL_DRIVER "Build the WDK kernel test driver (requires the Visual Studio generator)" OFF)
if(DAP_DBGENG_BUILD_KERNEL_DRIVER)
  add_subdirectory(test-targets/sys)
endif()
