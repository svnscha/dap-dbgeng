# FindWDKNuGet.cmake
#
# Locates the Windows Driver Kit (WDK) when it is delivered as NuGet packages
# (Microsoft.Windows.WDK.x64 + Microsoft.Windows.SDK.CPP) rather than the classic
# "Windows Kits\10" MSI install, and exposes a wdk_add_driver() helper.
#
# Why this is not the upstream FindWDK module: the NuGet WDK is split across
# several packages, and the kernel headers no longer sit next to the shared SDK
# headers under one root:
#
#   microsoft.windows.wdk.x64/<pkgver>/c     -> Include/<sdkver>/km (+ km/crt),
#                                               Lib/<sdkver>/km/x64/*.lib, bin tools
#   microsoft.windows.sdk.cpp/<pkgver>/c     -> Include/<sdkver>/{shared,um,ucrt}
#                                               (ntdef.h, ntstatus.h, ... live here)
#
# Both packages agree on the *SDK version* folder name (e.g. 10.0.26100.0) even
# though their package versions differ, so we pair them on that.
#
# Inputs (optional cache variables):
#   WDK_NUGET_PACKAGES_DIR  Extra NuGet packages root to search first.
#   WDK_SDK_VERSION         Pin a specific SDK version folder (e.g. 10.0.26100.0).
#                           Defaults to the highest version present in both packages.
#
# Outputs:
#   WDKNuGet_FOUND, WDKNuGet_SDK_VERSION, WDKNuGet_INCLUDE_DIRS,
#   WDKNuGet_KM_LIB_DIR, WDKNuGet_STAMPINF
#
# Provides:
#   wdk_add_driver(<target> <sources...>)   builds a kernel-mode .sys

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "FindWDKNuGet: kernel drivers can only be built on Windows.")
endif()

# MSVC's default Debug flags include /RTC1 (run-time checks), which emit calls to
# _RTC_InitBase/_RTC_Shutdown from the user-mode CRT — unavailable in a kernel
# driver linked with /NODEFAULTLIB. Strip run-time checks from the default flags.
foreach(_lang C CXX)
    if(DEFINED CMAKE_${_lang}_FLAGS_DEBUG)
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_${_lang}_FLAGS_DEBUG "${CMAKE_${_lang}_FLAGS_DEBUG}")
    endif()
endforeach()

# --- Candidate NuGet package roots ------------------------------------------------
# Search order: explicit override, NUGET_PACKAGES env, the per-user global cache,
# then a local ./packages dir (populated by Restore-Wdk.ps1 / `nuget restore`).
set(_wdk_roots "")
if(WDK_NUGET_PACKAGES_DIR)
    list(APPEND _wdk_roots "${WDK_NUGET_PACKAGES_DIR}")
endif()
if(DEFINED ENV{NUGET_PACKAGES})
    list(APPEND _wdk_roots "$ENV{NUGET_PACKAGES}")
endif()
if(DEFINED ENV{USERPROFILE})
    list(APPEND _wdk_roots "$ENV{USERPROFILE}/.nuget/packages")
endif()
list(APPEND _wdk_roots "${CMAKE_CURRENT_SOURCE_DIR}/packages")
list(APPEND _wdk_roots "${CMAKE_SOURCE_DIR}/packages")

# --- Locate the WDK kernel headers (ntddk.h) --------------------------------------
# Global-cache layout is   <root>/<id>/<pkgver>/c/...
# `nuget restore` layout is <root>/<id>.<pkgver>/c/...   -- match both.
set(_ntddk "")
foreach(_root IN LISTS _wdk_roots)
    file(GLOB _g1 "${_root}/microsoft.windows.wdk.x64/*/c/Include/*/km/ntddk.h")
    file(GLOB _g2 "${_root}/microsoft.windows.wdk.x64.*/c/Include/*/km/ntddk.h")
    list(APPEND _ntddk ${_g1} ${_g2})
endforeach()

# --- Locate the shared SDK headers (ntdef.h) --------------------------------------
set(_ntdef "")
foreach(_root IN LISTS _wdk_roots)
    file(GLOB _g1 "${_root}/microsoft.windows.sdk.cpp/*/c/Include/*/shared/ntdef.h")
    file(GLOB _g2 "${_root}/microsoft.windows.sdk.cpp.*/c/Include/*/shared/ntdef.h")
    list(APPEND _ntdef ${_g1} ${_g2})
endforeach()

# Map SDK-version -> the WDK "c" root and the SDK "c" root that provide it.
# Walk the path with get_filename_component (no regex) so policy CMP0010 doesn't
# choke on backslash escapes in version-folder names:
#   <c>/Include/<sdkver>/{km|shared}/<header>
function(_wdk_split_header _header _out_ver _out_croot)
    get_filename_component(_dir "${_header}" DIRECTORY)      # .../Include/<ver>/km
    get_filename_component(_verdir "${_dir}" DIRECTORY)      # .../Include/<ver>
    get_filename_component(_ver "${_verdir}" NAME)           # <ver>
    get_filename_component(_incdir "${_verdir}" DIRECTORY)   # .../Include
    get_filename_component(_croot "${_incdir}" DIRECTORY)    # .../c
    set(${_out_ver} "${_ver}" PARENT_SCOPE)
    set(${_out_croot} "${_croot}" PARENT_SCOPE)
endfunction()

set(_versions "")
foreach(_h IN LISTS _ntddk)
    _wdk_split_header("${_h}" _ver _croot)
    set(_wdk_c_for_${_ver} "${_croot}")
    list(APPEND _versions "${_ver}")
endforeach()
foreach(_h IN LISTS _ntdef)
    _wdk_split_header("${_h}" _ver _croot)
    set(_sdk_c_for_${_ver} "${_croot}")
endforeach()

# Keep only versions present in BOTH packages, then pick the pinned/highest one.
set(_usable "")
foreach(_ver IN LISTS _versions)
    if(DEFINED _wdk_c_for_${_ver} AND DEFINED _sdk_c_for_${_ver})
        list(APPEND _usable "${_ver}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _usable)

if(WDK_SDK_VERSION)
    if(NOT WDK_SDK_VERSION IN_LIST _usable)
        message(FATAL_ERROR "FindWDKNuGet: requested WDK_SDK_VERSION='${WDK_SDK_VERSION}' not found "
                            "(WDK+SDK pair). Available: ${_usable}")
    endif()
    set(_pick "${WDK_SDK_VERSION}")
else()
    list(SORT _usable COMPARE NATURAL)
    list(LENGTH _usable _n)
    if(_n EQUAL 0)
        set(_pick "")
    else()
        math(EXPR _last "${_n} - 1")
        list(GET _usable ${_last} _pick)
    endif()
endif()

if(_pick)
    set(_wdk_c "${_wdk_c_for_${_pick}}")
    set(_sdk_c "${_sdk_c_for_${_pick}}")

    set(WDKNuGet_SDK_VERSION "${_pick}" CACHE STRING "Resolved WDK/SDK include version" FORCE)
    set(WDKNuGet_INCLUDE_DIRS
        "${_wdk_c}/Include/${_pick}/km"
        "${_wdk_c}/Include/${_pick}/km/crt"
        "${_wdk_c}/Include/${_pick}/shared"
        "${_sdk_c}/Include/${_pick}/shared"
        "${_sdk_c}/Include/${_pick}/um"
        CACHE STRING "WDK/SDK kernel include directories" FORCE)
    set(WDKNuGet_KM_LIB_DIR "${_wdk_c}/Lib/${_pick}/km/x64"
        CACHE PATH "WDK kernel-mode x64 import library directory" FORCE)

    # WDK tools live under bin/<sdkver>/x64; the bin version can lag the include
    # version, so glob for stampinf rather than assuming the folder name.
    file(GLOB _stampinf "${_wdk_c}/bin/*/x64/stampinf.exe")
    if(_stampinf)
        list(GET _stampinf 0 _stampinf0)
        set(WDKNuGet_STAMPINF "${_stampinf0}" CACHE FILEPATH "stampinf.exe" FORCE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WDKNuGet
    REQUIRED_VARS WDKNuGet_INCLUDE_DIRS WDKNuGet_KM_LIB_DIR WDKNuGet_SDK_VERSION
    FAIL_MESSAGE "Could not find a WDK + SDK NuGet package pair. Run test-targets/sys/Restore-Wdk.ps1, or install the WDK via NuGet (Microsoft.Windows.WDK.x64 + Microsoft.Windows.SDK.CPP).")

# --- wdk_add_driver(target sources...) --------------------------------------------
function(wdk_add_driver _target)
    add_executable(${_target} ${ARGN})

    # Emit a .sys instead of .exe.
    set_target_properties(${_target} PROPERTIES SUFFIX ".sys")

    # WDK headers are third-party: treat as SYSTEM so their warnings stay quiet.
    target_include_directories(${_target} SYSTEM PRIVATE ${WDKNuGet_INCLUDE_DIRS})

    target_compile_definitions(${_target} PRIVATE
        WINNT=1
        _AMD64_ AMD64 _WIN64
        DEPRECATE_DDK_FUNCTIONS=1
        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
        $<$<CONFIG:Debug>:DBG=1>)

    # /kernel selects kernel-mode code generation (no C++ EH/RTTI, /GS on, etc.).
    target_compile_options(${_target} PRIVATE /kernel /Zp8 /Oi)

    target_link_directories(${_target} PRIVATE ${WDKNuGet_KM_LIB_DIR})
    target_link_libraries(${_target} PRIVATE
        ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib)

    # Driver link contract: native subsystem, /DRIVER, GS-wrapped entry, no CRT.
    target_link_options(${_target} PRIVATE
        /DRIVER
        /SUBSYSTEM:NATIVE
        /ENTRY:GsDriverEntry
        /NODEFAULTLIB
        /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:NO
        /MERGE:_TEXT=.text
        /MERGE:_PAGE=PAGE
        /SECTION:INIT,d)
endfunction()
