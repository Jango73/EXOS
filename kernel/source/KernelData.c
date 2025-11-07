
#include "Kernel.h"
#include "Socket.h"
#include "utils/Helpers.h"

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

KERNELDATA SECTION(".data") Kernel = {
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
