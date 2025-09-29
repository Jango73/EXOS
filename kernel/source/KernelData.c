
#include "../include/Kernel.h"
#include "../include/Socket.h"

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

KERNELDATA_I386 SECTION(".data") Kernel_i386 = {.GDT = 0, .TSS = 0, .PPB = (U8*)0};

KERNELDATA SECTION(".data") Kernel = {
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .PCIDevice = &PciDeviceList,
    .FileSystem = &FileSystemList,
    .File = &FileList,
    .TCPConnection = &TCPConnectionList,
    .Socket = &SocketList,
    .SystemFS = {
        .Header = {
            .ID = ID_FILESYSTEM,
            .References = 1,
            .Next = NULL,
            .Prev = NULL,
            .Mutex = EMPTY_MUTEX,
            .Driver = &SystemFSDriver,
            .Name = "System"
        },
        .Root = NULL
    },
    .UserAccount = &UserAccountList,
    .UserSessions = NULL,
    .Configuration = NULL,
    .LanguageCode = "en-US",
    .KeyboardCode = "fr-FR",
    .CPU = {.Name = "", .Type = 0, .Family = 0, .Model = 0, .Stepping = 0, .Features = 0},
    .MinimumQuantum = 10,
    .UserDatabase = NULL};
