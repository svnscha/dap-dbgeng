// hello.c — minimal WDM "Hello World" kernel-mode driver.
//
// Loaded as a legacy software driver (no PnP, no INF needed):
//   sc create hello type= kernel binPath= C:\path\to\hello.sys
//   sc start  hello        // -> DriverEntry runs, prints, returns STATUS_SUCCESS
//   sc stop   hello        // -> HelloUnload runs
//   sc delete hello
//
// Output goes to the kernel debugger / DebugView. DriverEntry logs at ERROR level
// so it shows without touching the DbgPrint filter mask; the verbose lines use
// INFO level (enable with: HKLM\SYSTEM\CurrentControlSet\Control
// \Session Manager\Debug Print Filter\DEFAULT = 0xFFFFFFFF).
//
// This is the target we use to exercise the adapter's kernel-debug attach: load it
// on a KD-enabled VM, attach with the adapter (kernel: true), and confirm the
// break-in, DbgPrint output, and a breakpoint on DriverEntry/HelloUnload.

#include <ntddk.h>

// Set -DHELLO_BREAK_ON_ENTRY=1 to have the driver break into an attached kernel
// debugger the moment it loads (only when a debugger is actually present, so it
// won't wedge a normal boot). Handy for catching DriverEntry without racing it.
#ifndef HELLO_BREAK_ON_ENTRY
#define HELLO_BREAK_ON_ENTRY 0
#endif

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD HelloUnload;

_Use_decl_annotations_ VOID HelloUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hello] DriverUnload\n");
}

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverUnload = HelloUnload;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hello] Hello from the kernel! DriverObject=%p\n",
               DriverObject);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hello] RegistryPath=%wZ\n", RegistryPath);

#if HELLO_BREAK_ON_ENTRY
    if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT)
    {
        DbgBreakPoint();
    }
#endif

    return STATUS_SUCCESS;
}
