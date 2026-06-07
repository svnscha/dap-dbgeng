// hello.c - a small but real WDM control-device driver.
//
// Loaded as a legacy software driver (no PnP, no INF needed):
//   sc create hello type= kernel binPath= C:\path\to\hello.sys
//   sc start  hello        // -> DriverEntry: creates \Device\Hello + \DosDevices\Hello
//   sc stop   hello        // -> HelloUnload: tears them down
//   sc delete hello
//
// What it exposes: a control device that user mode can open by name and query
// with a single IOCTL. DeviceIoControl(IOCTL_HELLO_GET_INFO) returns a
// HELLO_INFO (the driver version plus how many times the device has been opened
// since load). That gives the kernel-debug target something concrete to break on
// and inspect: dispatch routines, an IRP, a METHOD_BUFFERED transfer, and live
// driver state (the interlocked open counter).
//
// Output goes to the kernel debugger / DebugView. Lifecycle lines log at ERROR
// level so they show without touching the DbgPrint filter mask; the per-request
// lines use INFO level (enable with: HKLM\SYSTEM\CurrentControlSet\Control
// \Session Manager\Debug Print Filter\DEFAULT = 0xFFFFFFFF).

#include <ntddk.h>

#define HELLO_DRIVER_VERSION 0x00010000UL // 1.0

// Control device and its user-visible symbolic link. User mode opens \\.\Hello
// (CreateFile), which maps through \DosDevices\Hello to \Device\Hello.
static const UNICODE_STRING HelloDeviceName = RTL_CONSTANT_STRING(L"\\Device\\Hello");
static const UNICODE_STRING HelloSymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\Hello");

// DeviceIoControl(IOCTL_HELLO_GET_INFO) -> HELLO_INFO. METHOD_BUFFERED, so the
// I/O manager copies through Irp->AssociatedIrp.SystemBuffer.
#define IOCTL_HELLO_GET_INFO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _HELLO_INFO
{
    ULONG Version;   // HELLO_DRIVER_VERSION
    ULONG OpenCount; // handles opened against the device since load
} HELLO_INFO, *PHELLO_INFO;

// Live driver state: opens since load. Written from IRP_MJ_CREATE, so use
// interlocked access; an aligned LONG read is atomic on its own.
static volatile LONG HelloOpenCount = 0;

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD HelloUnload;
__drv_dispatchType(IRP_MJ_CREATE) DRIVER_DISPATCH HelloCreateClose;
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH HelloDeviceControl;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, HelloUnload)
#endif

// Complete an IRP with a status and an Information (bytes transferred) value.
static NTSTATUS HelloCompleteIrp(_Inout_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

// CREATE and CLOSE share a handler: opening bumps the live counter, and both
// just succeed so user mode can take and drop a handle to the control device.
_Use_decl_annotations_ NTSTATUS HelloCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    const PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (stack->MajorFunction == IRP_MJ_CREATE)
    {
        const LONG opened = InterlockedIncrement(&HelloOpenCount);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hello] open #%ld\n", opened);
    }

    return HelloCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

// The one IOCTL: copy a HELLO_INFO snapshot back to the caller.
_Use_decl_annotations_ NTSTATUS HelloDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    const PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    UNREFERENCED_PARAMETER(DeviceObject);

    if (stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_HELLO_GET_INFO)
    {
        return HelloCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(HELLO_INFO))
    {
        return HelloCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    // METHOD_BUFFERED: the output buffer is Irp->AssociatedIrp.SystemBuffer.
    PHELLO_INFO info = (PHELLO_INFO)Irp->AssociatedIrp.SystemBuffer;
    info->Version = HELLO_DRIVER_VERSION;
    info->OpenCount = (ULONG)HelloOpenCount;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[hello] IOCTL_HELLO_GET_INFO -> version=0x%08lx opens=%lu\n",
               info->Version, info->OpenCount);

    return HelloCompleteIrp(Irp, STATUS_SUCCESS, sizeof(HELLO_INFO));
}

_Use_decl_annotations_ VOID HelloUnload(PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    IoDeleteSymbolicLink((PUNICODE_STRING)&HelloSymbolicLink);
    if (DriverObject->DeviceObject != NULL)
    {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hello] unloaded\n");
}

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    status = IoCreateDevice(DriverObject, 0, (PUNICODE_STRING)&HelloDeviceName, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hello] IoCreateDevice failed: 0x%08x\n", status);
        return status;
    }

    status = IoCreateSymbolicLink((PUNICODE_STRING)&HelloSymbolicLink, (PUNICODE_STRING)&HelloDeviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[hello] IoCreateSymbolicLink failed: 0x%08x\n", status);
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->DriverUnload = HelloUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = HelloCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HelloCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HelloDeviceControl;

    deviceObject->Flags |= DO_BUFFERED_IO;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
               "[hello] loaded: \\Device\\Hello (\\\\.\\Hello), version 0x%08lx\n", HELLO_DRIVER_VERSION);

    return STATUS_SUCCESS;
}
