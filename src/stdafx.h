#pragma once

// =============================================================================
// stdafx.h - precompiled header
//
// Pulled in via target_precompile_headers() for every translation unit (see
// src/CMakeLists.txt and tests/CMakeLists.txt). Keep it to stable, widely used
// headers; project headers do not belong here.
// =============================================================================

// Trim rarely used Win32 surface and keep windows.h from defining min/max
// macros that collide with std::min / std::max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// Windows debug engine (dbgeng) and symbol helper.
#include <dbgeng.h>
#include <dbghelp.h>

// C++ standard library - common includes.
//
// This is the single include point for the standard library: project headers
// and sources rely on these being available via the PCH instead of repeating
// them. If you need a common header that is not here, add it here.
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Third-party dependencies (vcpkg).
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
