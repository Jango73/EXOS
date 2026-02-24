
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Kernel data

\************************************************************************/

#include "Kernel.h"
#include "Mouse.h"
#include "Socket.h"
#include "DriverGetters.h"
#include "drivers/KeyboardDrivers.h"
#include "drivers/MouseDrivers.h"
#include "utils/Helpers.h"
#include "process/Process.h"
#include "drivers/Keyboard.h"
#include "Log.h"

/************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

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

static LIST USBDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBInterfaceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBEndpointList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBStorageList = {
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

static LIST UnusedFileSystemList = {
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

static KERNELDATA DATA_SECTION Kernel = {
    .Drivers = &DriverList,
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .USBDevice = &USBDeviceList,
    .USBInterface = &USBInterfaceList,
    .USBEndpoint = &USBEndpointList,
    .USBStorage = &USBStorageList,
    .PCIDevice = &PciDeviceList,
    .NetworkDevice = &NetworkDeviceList,
    .Event = &EventList,
    .FileSystem = &FileSystemList,
    .UnusedFileSystem = &UnusedFileSystemList,
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
            .Mounted = TRUE,
            .Driver = &SystemFSDriver,
            .StorageUnit = NULL,
            .Partition = {
                .Scheme = PARTITION_SCHEME_VIRTUAL,
                .Type = FSID_NONE,
                .Format = PARTITION_FORMAT_UNKNOWN,
                .Index = 0,
                .Flags = 0,
                .StartSector = 0,
                .NumSectors = 0,
                .TypeGuid = {0}
            },
            .Name = "System"
        },
        .Root = NULL
    },
    .HandleMap = {0},
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

    ListAddTail(Kernel.Drivers, ConsoleGetDriver());
    ListAddTail(Kernel.Drivers, KernelLogGetDriver());
    ListAddTail(Kernel.Drivers, MemoryManagerGetDriver());
    ListAddTail(Kernel.Drivers, TaskSegmentsGetDriver());
    ListAddTail(Kernel.Drivers, InterruptsGetDriver());
    ListAddTail(Kernel.Drivers, KernelProcessGetDriver());
    ListAddTail(Kernel.Drivers, ACPIGetDriver());
    ListAddTail(Kernel.Drivers, LocalAPICGetDriver());
    ListAddTail(Kernel.Drivers, IOAPICGetDriver());
    ListAddTail(Kernel.Drivers, InterruptControllerGetDriver());
    ListAddTail(Kernel.Drivers, DeviceInterruptGetDriver());
    ListAddTail(Kernel.Drivers, DeferredWorkGetDriver());
    ListAddTail(Kernel.Drivers, SerialMouseGetDriver());
    ListAddTail(Kernel.Drivers, ClockGetDriver());
    ListAddTail(Kernel.Drivers, PCIGetDriver());
    ListAddTail(Kernel.Drivers, KeyboardSelectorGetDriver());
    ListAddTail(Kernel.Drivers, USBMouseGetDriver());
    ListAddTail(Kernel.Drivers, USBStorageGetDriver());
    ListAddTail(Kernel.Drivers, ATADiskGetDriver());
    ListAddTail(Kernel.Drivers, SATADiskGetDriver());
    ListAddTail(Kernel.Drivers, RAMDiskGetDriver());
    ListAddTail(Kernel.Drivers, FileSystemGetDriver());
    ListAddTail(Kernel.Drivers, NetworkManagerGetDriver());
    ListAddTail(Kernel.Drivers, UserAccountGetDriver());
    ListAddTail(Kernel.Drivers, VESAGetDriver());
}

/************************************************************************/

/**
 * @brief Retrieves the kernel driver list.
 * @return Pointer to the driver list.
 */
LPLIST GetDriverList(void) {
    return Kernel.Drivers;
}

/************************************************************************/

/**
 * @brief Retrieves the desktop list.
 * @return Pointer to the desktop list.
 */
LPLIST GetDesktopList(void) {
    return Kernel.Desktop;
}

/************************************************************************/

/**
 * @brief Retrieves the process list.
 * @return Pointer to the process list.
 */
LPLIST GetProcessList(void) {
    return Kernel.Process;
}

/************************************************************************/

/**
 * @brief Retrieves the task list.
 * @return Pointer to the task list.
 */
LPLIST GetTaskList(void) {
    return Kernel.Task;
}

/************************************************************************/

/**
 * @brief Retrieves the mutex list.
 * @return Pointer to the mutex list.
 */
LPLIST GetMutexList(void) {
    return Kernel.Mutex;
}

/************************************************************************/

/**
 * @brief Retrieves the disk list.
 * @return Pointer to the disk list.
 */
LPLIST GetDiskList(void) {
    return Kernel.Disk;
}

/************************************************************************/

/**
 * @brief Retrieves the USB device list.
 * @return Pointer to the USB device list.
 */
LPLIST GetUsbDeviceList(void) {
    return Kernel.USBDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the USB interface list.
 * @return Pointer to the USB interface list.
 */
LPLIST GetUsbInterfaceList(void) {
    return Kernel.USBInterface;
}

/************************************************************************/

/**
 * @brief Retrieves the USB endpoint list.
 * @return Pointer to the USB endpoint list.
 */
LPLIST GetUsbEndpointList(void) {
    return Kernel.USBEndpoint;
}

/************************************************************************/

/**
 * @brief Retrieves the USB storage list.
 * @return Pointer to the USB storage list.
 */
LPLIST GetUsbStorageList(void) {
    return Kernel.USBStorage;
}

/************************************************************************/

/**
 * @brief Retrieves the PCI device list.
 * @return Pointer to the PCI device list.
 */
LPLIST GetPCIDeviceList(void) {
    return Kernel.PCIDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the network device list.
 * @return Pointer to the network device list.
 */
LPLIST GetNetworkDeviceList(void) {
    return Kernel.NetworkDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the event list.
 * @return Pointer to the event list.
 */
LPLIST GetEventList(void) {
    return Kernel.Event;
}

/************************************************************************/

/**
 * @brief Retrieves the file system list.
 * @return Pointer to the file system list.
 */
LPLIST GetFileSystemList(void) {
    return Kernel.FileSystem;
}

/************************************************************************/

/**
 * @brief Retrieves the list of discovered but not mounted file systems.
 * @return Pointer to the unused file system list.
 */
LPLIST GetUnusedFileSystemList(void) {
    return Kernel.UnusedFileSystem;
}

/************************************************************************/

/**
 * @brief Retrieves the open file list.
 * @return Pointer to the file list.
 */
LPLIST GetFileList(void) {
    return Kernel.File;
}

/************************************************************************/

/**
 * @brief Retrieves the TCP connection list.
 * @return Pointer to the TCP connection list.
 */
LPLIST GetTCPConnectionList(void) {
    return Kernel.TCPConnection;
}

/************************************************************************/

/**
 * @brief Retrieves the socket list.
 * @return Pointer to the socket list.
 */
LPLIST GetSocketList(void) {
    return Kernel.Socket;
}

/************************************************************************/

/**
 * @brief Retrieves the user session list.
 * @return Pointer to the user session list.
 */
LPLIST GetUserSessionList(void) {
    return Kernel.UserSessions;
}

/************************************************************************/

/**
 * @brief Sets the user session list pointer.
 * @param List Pointer to the user session list.
 */
void SetUserSessionList(LPLIST List) {
    Kernel.UserSessions = List;
}

/************************************************************************/

/**
 * @brief Retrieves the user account list.
 * @return Pointer to the user account list.
 */
LPLIST GetUserAccountList(void) {
    return Kernel.UserAccount;
}

/************************************************************************/

/**
 * @brief Sets the user account list pointer.
 * @param List Pointer to the user account list.
 */
void SetUserAccountList(LPLIST List) {
    Kernel.UserAccount = List;
}

/************************************************************************/

/**
 * @brief Retrieves the global file system info structure.
 * @return Pointer to the file system info structure.
 */
FILESYSTEM_GLOBAL_INFO* GetFileSystemGlobalInfo(void) {
    return &(Kernel.FileSystemInfo);
}

/************************************************************************/

/**
 * @brief Retrieves the SystemFS backing structure.
 * @return Pointer to the SystemFS structure.
 */
LPSYSTEMFSFILESYSTEM GetSystemFSData(void) {
    return &(Kernel.SystemFS);
}

/************************************************************************/

/**
 * @brief Retrieves the object termination cache.
 * @return Pointer to the cache.
 */
LPCACHE GetObjectTerminationCache(void) {
    return &(Kernel.ObjectTerminationCache);
}

/************************************************************************/

/**
 * @brief Retrieves the handle map.
 * @return Pointer to the handle map.
 */
LPHANDLE_MAP GetHandleMap(void) {
    return &(Kernel.HandleMap);
}

/**
 * @brief Retrieves the CPU information storage.
 * @return Pointer to the CPU information structure.
 */
LPCPUINFORMATION GetKernelCPUInfo(void) {
    return &(Kernel.CPU);
}

/************************************************************************/

/**
 * @brief Retrieves the deferred work wait timeout.
 * @return Timeout in milliseconds.
 */
UINT GetDeferredWorkWaitTimeout(void) {
    return Kernel.DeferredWorkWaitTimeoutMS;
}

/************************************************************************/

/**
 * @brief Sets the deferred work wait timeout.
 * @param Timeout Timeout in milliseconds.
 */
void SetDeferredWorkWaitTimeout(UINT Timeout) {
    Kernel.DeferredWorkWaitTimeoutMS = Timeout;
}

/************************************************************************/

/**
 * @brief Retrieves the deferred work poll delay.
 * @return Poll delay in milliseconds.
 */
UINT GetDeferredWorkPollDelay(void) {
    return Kernel.DeferredWorkPollDelayMS;
}

/************************************************************************/

/**
 * @brief Sets the deferred work poll delay.
 * @param Delay Poll delay in milliseconds.
 */
void SetDeferredWorkPollDelay(UINT Delay) {
    Kernel.DeferredWorkPollDelayMS = Delay;
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
    LPDRIVER UsbDriver = USBMouseGetDriver();
    if (UsbDriver != NULL && UsbDriver->Command(DF_MOUSE_HAS_DEVICE, 0) == 1U) {
        return UsbDriver;
    }

    return SerialMouseGetDriver();
}

/************************************************************************/

/**
 * @brief Retrieves the graphics driver descriptor.
 * @return Pointer to the graphics driver.
 */
LPDRIVER GetGraphicsDriver(void) {
    return VESAGetDriver();
}

/************************************************************************/

/**
 * @brief Retrieves the default file system driver descriptor.
 * @return Pointer to the default file system driver.
 */
LPDRIVER GetDefaultFileSystemDriver(void) {
    return EXFSGetDriver();
}
