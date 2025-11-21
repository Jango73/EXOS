
#include "Kernel.h"
#include "Socket.h"
#include "utils/Helpers.h"

/************************************************************************/

extern DRIVER ConsoleDriver;
extern DRIVER KernelLogDriver;
extern DRIVER MemoryManagerDriver;
extern DRIVER TaskSegmentsDriver;
extern DRIVER InterruptsDriver;
extern DRIVER KernelProcessDriver;
extern DRIVER ACPIDriver;
extern DRIVER LocalAPICDriver;
extern DRIVER IOAPICDriver;
extern DRIVER InterruptControllerDriver;
extern DRIVER StdKeyboardDriver;
extern DRIVER SerialMouseDriver;
extern DRIVER ClockDriver;
extern DRIVER PCIDriver;
extern DRIVER ATADiskDriver;
extern DRIVER SATADiskDriver;
extern DRIVER RAMDiskDriver;
extern DRIVER FileSystemDriver;
extern DRIVER DeviceInterruptDriver;
extern DRIVER DeferredWorkDriver;
extern DRIVER NetworkManagerDriver;
extern DRIVER UserAccountDriver;
extern DRIVER VESADriver;

extern DRIVER EXFSDriver;

/************************************************************************/

static LIST DriverList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST DesktopList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST ProcessList = {
    .First = (LPLISTNODE)&KernelProcess,
    .Last = (LPLISTNODE)&KernelProcess,
    .Current = (LPLISTNODE)&KernelProcess,
    .NumItems = 1,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST TaskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST MutexList = {
    .First = (LPLISTNODE)&KernelMutex,
    .Last = (LPLISTNODE)&ConsoleMutex,
    .Current = (LPLISTNODE)&KernelMutex,
    .NumItems = 12,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST DiskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST PciDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST NetworkDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST EventList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST FileSystemList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST FileList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST TCPConnectionList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST SocketList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = SocketDestructor};

/************************************************************************/

static LIST UserAccountList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

KERNELDATA DATA_SECTION Kernel = {
    .Drivers = &DriverList,
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .PCIDevice = &PciDeviceList,
    .NetworkDevice = &NetworkDeviceList,
    .Event = &EventList,
    .FileSystem = &FileSystemList,
    .File = &FileList,
    .TCPConnection = &TCPConnectionList,
    .Socket = &SocketList,
    .UserSessions = NULL,
    .UserAccount = &UserAccountList,
    .FileSystemInfo = {.ActivePartitionName = ""},
    .SystemFS = {
        .Header = {
            .TypeID = KOID_FILESYSTEM,
            .References = 1,
            .Next = NULL,
            .Prev = NULL,
            .Mutex = EMPTY_MUTEX,
            .Driver = &SystemFSDriver,
            .Name = "System"
        },
        .Root = NULL
    },
    .HandleMap = {0},
    .PPBSize = 0,
    .PPB = NULL,
    .CPU = {.Name = "", .Type = 0, .Family = 0, .Model = 0, .Stepping = 0, .Features = 0},
    .Configuration = NULL,
    .MinimumQuantum = 10,
    .MaximumQuantum = 50,
    .DeferredWorkWaitTimeoutMS = DEFERRED_WORK_WAIT_TIMEOUT_MS,
    .DeferredWorkPollDelayMS = DEFERRED_WORK_POLL_DELAY_MS,
    .DoLogin = 0,
    .LanguageCode = "en-US",
    .KeyboardCode = "fr-FR"
    };

/************************************************************************/

/**
 * @brief Populates the kernel driver list in initialization order.
 */
void InitializeDriverList(void) {
    if (Kernel.Drivers == NULL || Kernel.Drivers->NumItems != 0) {
        return;
    }

    ListAddTail(Kernel.Drivers, &ConsoleDriver);
    ListAddTail(Kernel.Drivers, &KernelLogDriver);
    ListAddTail(Kernel.Drivers, &MemoryManagerDriver);
    ListAddTail(Kernel.Drivers, &TaskSegmentsDriver);
    ListAddTail(Kernel.Drivers, &InterruptsDriver);
    ListAddTail(Kernel.Drivers, &KernelProcessDriver);
    ListAddTail(Kernel.Drivers, &ACPIDriver);
    ListAddTail(Kernel.Drivers, &LocalAPICDriver);
    ListAddTail(Kernel.Drivers, &IOAPICDriver);
    ListAddTail(Kernel.Drivers, &InterruptControllerDriver);
    ListAddTail(Kernel.Drivers, &DeviceInterruptDriver);
    ListAddTail(Kernel.Drivers, &DeferredWorkDriver);
    ListAddTail(Kernel.Drivers, &StdKeyboardDriver);
    ListAddTail(Kernel.Drivers, &SerialMouseDriver);
    ListAddTail(Kernel.Drivers, &ClockDriver);
    ListAddTail(Kernel.Drivers, &PCIDriver);
    ListAddTail(Kernel.Drivers, &ATADiskDriver);
    ListAddTail(Kernel.Drivers, &SATADiskDriver);
    ListAddTail(Kernel.Drivers, &RAMDiskDriver);
    ListAddTail(Kernel.Drivers, &FileSystemDriver);
    ListAddTail(Kernel.Drivers, &NetworkManagerDriver);
    ListAddTail(Kernel.Drivers, &UserAccountDriver);
    ListAddTail(Kernel.Drivers, &VESADriver);
}
