# test-targets/sys - Hello kernel driver

A small WDM control-device driver (`hello.sys`) used to exercise the adapter's
**kernel-debug attach**. Load it on a KD-enabled VM, attach with the adapter
(`kernel: true`), and confirm break-in, `DbgPrint` output, and breakpoints on
`DriverEntry` / `HelloDeviceControl`.

It builds against the **WDK delivered via NuGet** - no classic "Windows Kits"
WDK install required - wired into CMake by [`cmake/FindWDKNuGet.cmake`](cmake/FindWDKNuGet.cmake).

## What it does

[`src/hello.c`](src/hello.c) is a legacy software driver. `DriverEntry` creates a
control device (`\Device\Hello`) and a symbolic link (`\DosDevices\Hello`, i.e.
`\\.\Hello` from user mode), then registers CREATE/CLOSE/DEVICE_CONTROL dispatch
routines and `HelloUnload`. The single IOCTL, `IOCTL_HELLO_GET_INFO`, returns a
`HELLO_INFO` (the driver version plus the number of opens since load). Output goes
to the kernel debugger / DebugView; lifecycle lines log at `DPFLTR_ERROR_LEVEL` so
they show without changing the `DbgPrint` filter mask, and the per-request lines
use `DPFLTR_INFO_LEVEL`.

## Prerequisites

- Visual Studio 2022 with the **Desktop development with C++** workload (provides
  `cl.exe`, the VS CMake, and `dumpbin`).
- The WDK + SDK NuGet packages. Visual Studio restores them automatically from
  [`packages.config`](packages.config); on a fresh box without that, run:

  ```powershell
  ./test-targets/sys/Restore-Wdk.ps1
  ```

  `FindWDKNuGet.cmake` finds them in the per-user global cache
  (`%USERPROFILE%\.nuget\packages`) or the local `.\packages` dir, pairing the
  `Microsoft.Windows.WDK.x64` (kernel headers/libs/tools) and
  `Microsoft.Windows.SDK.CPP` (shared/um headers) packages on a common SDK
  version (e.g. `10.0.26100.0`). Pin a specific one with
  `-DWDK_SDK_VERSION=10.0.26100.0`; otherwise the highest available is used.

## Build

```powershell
# From the repo root (or: npm run build:sys)
cmake -S test-targets/sys -B test-targets/sys/build -G "Visual Studio 17 2022" -A x64
cmake --build test-targets/sys/build --config Debug
# -> test-targets/sys/build/Debug/hello.sys  (subsystem: Native, entry: GsDriverEntry)
```

Verify the PE if you like: `dumpbin /headers test-targets/sys/build/Debug/hello.sys` should
show `machine (x64)`, `subsystem (Native)`, entry `GsDriverEntry`.

## Load it on the test VM

The driver is unsigned, so the VM must allow test-signed/unsigned kernel code.
**Do this only on a throwaway VM.**

1. On the VM (elevated), enable test signing and reboot. If Secure Boot is on,
   disable it in the VM firmware first, or test signing won't take effect:

   ```cmd
   bcdedit /set testsigning on
   shutdown /r /t 0
   ```

   If Memory Integrity / HVCI is enabled, an *unsigned* binary still won't load -
   either disable it, or test-sign `hello.sys` with a self-signed cert
   (`New-SelfSignedCertificate` + `signtool sign /fd sha256 ...`, then trust the
   cert in Root + Trusted Publishers).

2. Copy `hello.sys` to the VM, then create and start the service:

   ```cmd
   sc create hello type= kernel binPath= C:\path\to\hello.sys
   sc start  hello
   sc stop   hello
   sc delete hello
   ```

   (Note the required space after each `=`.)

3. See the output: run **DebugView** elevated with *Capture Kernel* + *Enable
   Verbose Kernel Output*, or watch it in the attached kernel debugger. To also
   see the `INFO`-level line, set
   `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter\DEFAULT = 0xFFFFFFFF`.

## Attach the kernel debugger (the actual test)

This is the target for verifying the adapter's `kernel: true` attach (see
`human-backlog.md`). Set up KD transport on the VM (`bcdedit /debug on` plus a
KDNET or COM-pipe transport), attach from the adapter with the matching
`connectionString`, then `sc start hello` and confirm:

- the break-in presents a paused target,
- `DriverEntry`'s `DbgPrint` appears,
- a breakpoint on `DriverEntry` / `HelloDeviceControl` is hit.
