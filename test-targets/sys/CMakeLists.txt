cmake_minimum_required(VERSION 3.20)

# Kernel-mode "Hello World" driver, built against the WDK delivered via NuGet
# (Microsoft.Windows.WDK.x64 + Microsoft.Windows.SDK.CPP). See README.md.
#
# Configure/build (x64, matching test-targets/testapp):
#   cmake -S test-targets/sys -B test-targets/sys/build -G "Visual Studio 17 2022" -A x64
#   cmake --build test-targets/sys/build --config Debug
# Output: test-targets/sys/build/Debug/hello.sys

project(hello-driver C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure with -A x64 — this driver targets x64 only.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(WDKNuGet REQUIRED)

message(STATUS "WDK (NuGet) SDK version: ${WDKNuGet_SDK_VERSION}")

wdk_add_driver(hello src/hello.c)
