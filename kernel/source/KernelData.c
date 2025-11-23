
#include "Kernel.h"
#include "Socket.h"
#include "utils/Helpers.h"
#include "process/Process.h"
#include "drivers/Keyboard.h"

/************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

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
    .FocusedDesktop = &MainDesktop,
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

/************************************************************************/

/**
 * @brief Retrieves the kernel configuration.
 * @return Pointer to the parsed configuration or NULL if not set.
 */
LPTOML GetConfiguration(void) {
    return Kernel.Configuration;
}

/************************************************************************/

/**
 * @brief Updates the kernel configuration pointer.
 * @param Configuration Parsed configuration to store.
 */
void SetConfiguration(LPTOML Configuration) {
    Kernel.Configuration = Configuration;
}

/************************************************************************/

/**
 * @brief Gets the login sequence flag.
 * @return TRUE when login is enabled.
 */
BOOL GetDoLogin(void) {
    return Kernel.DoLogin;
}

/************************************************************************/

/**
 * @brief Sets the login sequence flag.
 * @param DoLogin TRUE to enable login, FALSE to disable.
 */
void SetDoLogin(BOOL DoLogin) {
    Kernel.DoLogin = DoLogin;
}

/************************************************************************/

/**
 * @brief Retrieves the active language code.
 * @return Pointer to language code string.
 */
LPCSTR GetLanguageCode(void) {
    return Kernel.LanguageCode;
}

/************************************************************************/

/**
 * @brief Updates the active language code.
 * @param LanguageCode Null-terminated language code.
 */
void SetLanguageCode(LPCSTR LanguageCode) {
    SAFE_USE(LanguageCode) { StringCopy(Kernel.LanguageCode, LanguageCode); }
}

/************************************************************************/

/**
 * @brief Retrieves the active keyboard code.
 * @return Pointer to keyboard code string.
 */
LPCSTR GetKeyboardCode(void) {
    return Kernel.KeyboardCode;
}

/************************************************************************/

/**
 * @brief Updates the active keyboard code.
 * @param KeyboardCode Null-terminated keyboard code.
 */
void SetKeyboardCode(LPCSTR KeyboardCode) {
    SAFE_USE(KeyboardCode) { StringCopy(Kernel.KeyboardCode, KeyboardCode); }
}

/************************************************************************/

/**
 * @brief Retrieves the configured minimum scheduler quantum.
 * @return Minimum quantum in milliseconds.
 */
UINT GetMinimumQuantum(void) {
    return Kernel.MinimumQuantum;
}

/************************************************************************/

/**
 * @brief Updates the configured minimum scheduler quantum.
 * @param MinimumQuantum Quantum in milliseconds.
 */
void SetMinimumQuantum(UINT MinimumQuantum) {
    Kernel.MinimumQuantum = MinimumQuantum;
}

/************************************************************************/

/**
 * @brief Retrieves the configured maximum scheduler quantum.
 * @return Maximum quantum in milliseconds.
 */
UINT GetMaximumQuantum(void) {
    return Kernel.MaximumQuantum;
}

/************************************************************************/

/**
 * @brief Updates the configured maximum scheduler quantum.
 * @param MaximumQuantum Quantum in milliseconds.
 */
void SetMaximumQuantum(UINT MaximumQuantum) {
    Kernel.MaximumQuantum = MaximumQuantum;
}

/************************************************************************/

/**
 * @brief Retrieve the desktop currently holding input focus.
 * @return Focused desktop pointer or NULL if none is set.
 */
LPDESKTOP GetFocusedDesktop(void) {
    return Kernel.FocusedDesktop;
}

/************************************************************************/

/**
 * @brief Set the desktop that holds input focus.
 * @param Desktop Desktop to focus, may be NULL to clear focus.
 */
void SetFocusedDesktop(LPDESKTOP Desktop) {
    LPDESKTOP PreviousDesktop = Kernel.FocusedDesktop;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        Kernel.FocusedDesktop = Desktop;

        if (Desktop->FocusedProcess == NULL) {
            Desktop->FocusedProcess = &KernelProcess;
        }
    } else {
        Kernel.FocusedDesktop = &MainDesktop;
        MainDesktop.FocusedProcess = &KernelProcess;
    }

    if (Kernel.FocusedDesktop != PreviousDesktop) {
        ClearKeyboardBuffer();
    }
}

/************************************************************************/

/**
 * @brief Retrieve the process currently holding input focus.
 * @return Focused process pointer or NULL if none is set.
 */
LPPROCESS GetFocusedProcess(void) {
    LPDESKTOP Desktop = Kernel.FocusedDesktop;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        SAFE_USE_VALID_ID(Desktop->FocusedProcess, KOID_PROCESS) {
            if (Desktop->FocusedProcess->Status == PROCESS_STATUS_DEAD) {
                Desktop->FocusedProcess = &KernelProcess;
                return &KernelProcess;
            }
            return Desktop->FocusedProcess;
        }
    }

    return &KernelProcess;
}

/************************************************************************/

/**
 * @brief Set the process that holds input focus.
 * @param Process Process to focus, may be NULL to clear focus.
 */
void SetFocusedProcess(LPPROCESS Process) {
    LPDESKTOP Desktop = Kernel.FocusedDesktop;
    LPDESKTOP PreviousDesktop = Kernel.FocusedDesktop;
    LPPROCESS PreviousProcess = NULL;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) { PreviousProcess = Desktop->FocusedProcess; }

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->Desktop != NULL) {
            Desktop = Process->Desktop;
            Kernel.FocusedDesktop = Desktop;
        }
    }

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        if (Desktop->FocusedProcess != Process) {
            Desktop->FocusedProcess = Process;
        }
    }

    if (Kernel.FocusedDesktop != PreviousDesktop || PreviousProcess != Process) {
        ClearKeyboardBuffer();
    }
}

/************************************************************************/

/**
 * @brief Retrieves basic CPU identification data.
 *
 * Populates the provided structure using CPUID information, including
 * vendor string, model and feature flags.
 *
 * @param Info Pointer to structure that receives CPU information.
 * @return TRUE on success.
 */
BOOL GetCPUInformation(LPCPUINFORMATION Info) {
    CPUIDREGISTERS Regs[8];

    MemorySet(Info, 0, sizeof(CPUINFORMATION));

    GetCPUID(Regs);

    //-------------------------------------
    // Fill name with register contents

    *((U32*)(Info->Name + 0)) = Regs[0].reg_EBX;
    *((U32*)(Info->Name + 4)) = Regs[0].reg_EDX;
    *((U32*)(Info->Name + 8)) = Regs[0].reg_ECX;
    Info->Name[12] = '\0';

    //-------------------------------------
    // Get model information if available

    Info->Type = (Regs[1].reg_EAX & INTEL_CPU_MASK_TYPE) >> INTEL_CPU_SHFT_TYPE;
    Info->Family = (Regs[1].reg_EAX & INTEL_CPU_MASK_FAMILY) >> INTEL_CPU_SHFT_FAMILY;
    Info->Model = (Regs[1].reg_EAX & INTEL_CPU_MASK_MODEL) >> INTEL_CPU_SHFT_MODEL;
    Info->Stepping = (Regs[1].reg_EAX & INTEL_CPU_MASK_STEPPING) >> INTEL_CPU_SHFT_STEPPING;
    Info->Features = Regs[1].reg_EDX;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieves the mouse driver descriptor.
 * @return Pointer to the mouse driver.
 */
LPDRIVER GetMouseDriver(void) {
    return &SerialMouseDriver;
}

/************************************************************************/

/**
 * @brief Retrieves the graphics driver descriptor.
 * @return Pointer to the graphics driver.
 */
LPDRIVER GetGraphicsDriver(void) {
    return &VESADriver;
}

/************************************************************************/

/**
 * @brief Retrieves the default file system driver descriptor.
 * @return Pointer to the default file system driver.
 */
LPDRIVER GetDefaultFileSystemDriver(void) {
    return &EXFSDriver;
}
