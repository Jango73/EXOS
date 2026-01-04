
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


    Kernel

\************************************************************************/

#include "Kernel.h"

#include "Autotest.h"
#include "Clock.h"
#include "Console.h"
#include "drivers/ACPI.h"
#include "drivers/Keyboard.h"
#include "File.h"
#include "Lang.h"
#include "Log.h"
#include "process/Process.h"
#include "process/Task.h"
#include "utils/Helpers.h"
#include "utils/TOML.h"
#include "utils/UUID.h"

/************************************************************************/


extern U32 DeadBeef;

/************************************************************************/

U32 EXOS_End SECTION(".end_mark") = 0x534F5845;

/************************************************************************/

void DoPageFault(void) {
    UINT* Table = (UINT*)0;
    for (UINT Index = 0; Index < KernelStartup.MemorySize / sizeof(UINT); Index++) {
        Table[Index] = 0;
    }
}

/************************************************************************/

/**
 * @brief Converts a kernel symbol address to its corresponding physical
 *        address.
 *
 * @param Symbol Linear address of the kernel symbol to translate.
 * @return Physical address associated with the provided symbol.
 */

PHYSICAL KernelToPhysical(LINEAR Symbol) {
    return KernelStartup.KernelPhysicalBase + (PHYSICAL)(Symbol - (LINEAR)VMA_KERNEL);
}

/************************************************************************/

/**
 * @brief Convert a kernel pointer into a user-visible handle.
 *
 * Allocates a new entry in the handle map and attaches the provided pointer
 * to it. Returns 0 when allocation or attachment fails.
 *
 * @param Pointer Kernel pointer that must be exposed to userland.
 * @return HANDLE Newly created handle or 0 on failure.
 */
HANDLE PointerToHandle(LINEAR Pointer) {
    if (Pointer == 0) {
        return 0;
    }

    LPHANDLE_MAP HandleMap = GetHandleMap();

    UINT ExistingHandle = 0;
    if (HandleMapFindHandleByPointer(HandleMap, Pointer, &ExistingHandle) == HANDLE_MAP_OK) {
        return ExistingHandle;
    }

    UINT Handle = 0;
    UINT Status = HandleMapAllocateHandle(HandleMap, &Handle);
    if (Status != HANDLE_MAP_OK) {
        return 0;
    }

    Status = HandleMapAttachPointer(HandleMap, Handle, Pointer);
    if (Status != HANDLE_MAP_OK) {
        HandleMapReleaseHandle(HandleMap, Handle);
        return 0;
    }

    return Handle;
}

/************************************************************************/

/**
 * @brief Resolve a user-visible handle back to its kernel pointer.
 *
 * @param Handle Handle supplied by userland.
 * @return LINEAR Kernel pointer or 0 when the handle is invalid.
 */
LINEAR HandleToPointer(HANDLE Handle) {
    if (Handle == 0) {
        return 0;
    }

    LINEAR Pointer = 0;
    UINT Status = HandleMapResolveHandle(GetHandleMap(), Handle, &Pointer);
    if (Status != HANDLE_MAP_OK) {
        return 0;
    }

    return Pointer;
}

/************************************************************************/

/**
 * @brief Ensure that a value representing a kernel object is a pointer.
 *
 * If the value already lies within kernel space (>= VMA_KERNEL), it is
 * returned as-is. Otherwise it is treated as a handle and resolved to
 * its kernel pointer.
 *
 * @param Value Either a kernel pointer or a user-visible handle.
 * @return LINEAR Kernel pointer or 0 on failure.
 */
LINEAR EnsureKernelPointer(LINEAR Value) {
    if (Value == 0) return 0;
    if (Value >= VMA_KERNEL) return Value;

    LINEAR Pointer = HandleToPointer((HANDLE)Value);
    return Pointer;
}

/************************************************************************/

/**
 * @brief Ensure that a value representing a kernel object is a handle.
 *
 * If the value already lies in user handle space (< VMA_KERNEL), it is
 * returned unchanged. Otherwise the kernel pointer is converted into a
 * handle via PointerToHandle().
 *
 * @param Value Either a kernel pointer or a user-visible handle.
 * @return HANDLE Handle value or 0 on failure.
 */
HANDLE EnsureHandle(LINEAR Value) {
    if (Value == 0) return 0;
    if (Value < VMA_KERNEL) {
        return (HANDLE)Value;
    }

    return PointerToHandle(Value);
}

/************************************************************************/

/**
 * @brief Detach and release a handle from the global handle map.
 *
 * @param Handle Handle to release; ignored when 0.
 */
void ReleaseHandle(HANDLE Handle) {
    if (Handle == 0) {
        return;
    }

    LINEAR Pointer = 0;
    UINT Status = HandleMapDetachPointer(GetHandleMap(), Handle, &Pointer);
    if (Status != HANDLE_MAP_OK && Status != HANDLE_MAP_ERROR_NOT_ATTACHED) {
        WARNING(TEXT("[ReleaseHandle] Detach failed handle=%u status=%u"), Handle, Status);
    }

    Status = HandleMapReleaseHandle(GetHandleMap(), Handle);
    if (Status != HANDLE_MAP_OK) {
        WARNING(TEXT("[ReleaseHandle] Release failed handle=%u status=%u"), Handle, Status);
    }
}

/************************************************************************/

/**
 * @brief Initialize focus defaults and the global input queue.
 */
static void InitializeFocusState(void) {
    LPLIST DesktopList = GetDesktopList();

    // Ensure the main desktop is registered in the kernel's desktop list
    if (DesktopList != NULL && DesktopList->First == NULL) {
        DEBUG(TEXT("[InitializeFocusState] Registering MainDesktop in desktop list"));
        ListAddHead(DesktopList, &MainDesktop);
    }

    if (GetFocusedDesktop() == NULL) {
        SetFocusedDesktop(&MainDesktop);
    }

    SetFocusedDesktop(GetFocusedDesktop());
    SetFocusedProcess(&KernelProcess);

    if (KernelProcess.Desktop == NULL) {
        KernelProcess.Desktop = GetFocusedDesktop();
    }

    SAFE_USE_VALID_ID(GetFocusedDesktop(), KOID_DESKTOP) {
        if (GetFocusedDesktop()->FocusedProcess == NULL) {
            GetFocusedDesktop()->FocusedProcess = &KernelProcess;
        }
    }
}

/************************************************************************/

/**
 * @brief Initializes the kernel minimum quantum time based on environment.
 *
 * Uses BARE_METAL compile-time flag to determine quantum time.
 * Also accounts for debug output overhead which slows execution.
 * Sets Kernel.MinimumQuantum and Kernel.MaximumQuantum to appropriate value.
 */
void InitializeQuantumTime(void) {
    // Set base quantum time based on environment
#if BARE_METAL == 1
    SetMinimumQuantum(10);  // Shorter quantum for bare-metal
    DEBUG(TEXT("[InitializeQuantumTime] Bare-metal mode, base quantum = %d ms"), GetMinimumQuantum());
#else
    SetMinimumQuantum(50);  // Longer quantum for emulation/virtualization
    DEBUG(TEXT("[InitializeQuantumTime] Emulation mode, base quantum = %d ms"), GetMinimumQuantum());
#endif

    if (SCHEDULING_DEBUG_OUTPUT == 1) {
        // Double quantum when scheduling debug is enabled (logs slow down execution)
        SetMinimumQuantum(GetMinimumQuantum() * 2);
        FINE_DEBUG(TEXT("[InitializeQuantumTime] Scheduling debug enabled, final quantum = %d ms"),
            GetMinimumQuantum());
    }

    SetMaximumQuantum(GetMinimumQuantum() * 4);
}

/************************************************************************/

/**
 * @brief Task that displays system time and mouse status.
 *
 * The parameter encodes console coordinates where the time is printed.
 * X is stored in the high 16 bits and Y in the low 16 bits.
 *
 * @param Param Encoded console position.
 * @return Always returns 0.
 */

U32 ClockTestTask(LPVOID Param) {
    TRACED_FUNCTION;

    STR Text[64];
    U32 X = ((U32)Param & 0xFFFF0000) >> 16;
    U32 Y = ((U32)Param & 0x0000FFFF) >> 0;
    U32 OldX = 0;
    U32 OldY = 0;

    UINT Time = 0;
    UINT OldTime = 0;

    FOREVER {
        Time = DoSystemCall(SYSCALL_GetSystemTime, SYSCALL_PARAM(0));

        if (Time - OldTime >= 1000) {
            OldTime = Time;
            MilliSecondsToHMS(Time, Text);
            OldX = Console.CursorX;
            OldY = Console.CursorY;
            SetConsoleCursorPosition(X, Y);
            ConsolePrint(Text);
            SetConsoleCursorPosition(OldX, OldY);
        }

        DoSystemCall(SYSCALL_Sleep, SYSCALL_PARAM(500));
    }

    TRACED_EPILOGUE("ClockTestTask");
    return 0;
}

/************************************************************************/

/**
 * @brief Logs memory map and kernel startup information.
 */

void DumpCriticalInformation(void) {
    DEBUG(TEXT("  Multiboot entry count = %d"), KernelStartup.MultibootMemoryEntryCount);

    for (U32 Index = 0; Index < KernelStartup.MultibootMemoryEntryCount; Index++) {
        DEBUG(TEXT("Multiboot entry %d : %p, %d, %d"), Index,
            U64_Low32(KernelStartup.MultibootMemoryEntries[Index].Base),
            U64_Low32(KernelStartup.MultibootMemoryEntries[Index].Length),
            (U32)KernelStartup.MultibootMemoryEntries[Index].Type);
    }

    DEBUG(TEXT("Virtual addresses"));
    DEBUG(TEXT("  VMA_RAM = %p"), VMA_RAM);
    DEBUG(TEXT("  VMA_VIDEO = %p"), VMA_VIDEO);
    DEBUG(TEXT("  VMA_CONSOLE = %p"), VMA_CONSOLE);
    DEBUG(TEXT("  VMA_USER = %p"), VMA_USER);
    DEBUG(TEXT("  VMA_LIBRARY = %p"), VMA_LIBRARY);
    DEBUG(TEXT("  VMA_KERNEL = %p"), VMA_KERNEL);

    DEBUG(TEXT("Kernel startup info:"));
    DEBUG(TEXT("  KernelPhysicalBase = %p"), KernelStartup.KernelPhysicalBase);
    DEBUG(TEXT("  KernelSize = %d"), KernelStartup.KernelSize);
    DEBUG(TEXT("  StackTop = %p"), KernelStartup.StackTop);
    DEBUG(TEXT("  IRQMask_21_RM = %x"), KernelStartup.IRQMask_21_RM);
    DEBUG(TEXT("  IRQMask_A1_RM = %x"), KernelStartup.IRQMask_A1_RM);
    DEBUG(TEXT("  MemorySize = %d"), KernelStartup.MemorySize);
    DEBUG(TEXT("  PageCount = %d"), KernelStartup.PageCount);
}

/************************************************************************/

/**
 * @brief Prints memory information and the operating system banner.
 */

static void Welcome(void) {

/*
    ConsolePrint(TEXT("███████╗██╗  ██╗ ██████╗ ███████╗\n"));
    ConsolePrint(TEXT("██╔════╝╚██╗██╔╝██╔═══██╗██╔════╝\n"));
    ConsolePrint(TEXT("█████╗   ╚═╝╚═╝ ██║   ██║███████╗\n"));
    ConsolePrint(TEXT("██╔══╝   ██╔██╗ ██║   ██║╚════██║\n"));
    ConsolePrint(TEXT("███████╗██╔╝ ██╗╚██████╔╝███████║\n"));
    ConsolePrint(TEXT("╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝\n"));
*/

    ConsolePrint(TEXT("#######\\ ##\\  ##\\  ######\\  #######\\ \n"));
    ConsolePrint(TEXT("##<----/ \\##\\##// ##/---##\\ ##/----/ \n"));
    ConsolePrint(TEXT("#####\\    \\-/\\-/  ##|   ##| #######\\ \n"));
    ConsolePrint(TEXT("##/--/    ##/##\\  ##|   ##| \\----##|  \n"));
    ConsolePrint(TEXT("#######\\ ##// ##\\ \\######// #######| \n"));
    ConsolePrint(TEXT("\\------/ \\-/  \\-/  \\-----/  \\------/ \n\n"));

    ConsolePrint(
        TEXT(
            "Extensible Operating System for %s computers\n"
            "Version %u.%u.%u - Copyright (c) 1999-2025 Jango73\n"
            ),
        Text_Architecture,
        EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR, EXOS_VERSION_PATCH
        );

/*
    ConsolePrint(TEXT("\nEXOS - "));
    SetConsoleBackColor(CONSOLE_BLUE);
    SetConsoleForeColor(CONSOLE_WHITE);
    ConsolePrint("Extensible");
    SetConsoleBackColor(CONSOLE_WHITE);
    SetConsoleForeColor(CONSOLE_BLACK);
    ConsolePrint(" Operating");
    SetConsoleBackColor(CONSOLE_RED);
    SetConsoleForeColor(CONSOLE_WHITE);
    ConsolePrint(" System   ");
    SetConsoleBackColor(0);
    SetConsoleForeColor(CONSOLE_GRAY);
    ConsolePrint(
        TEXT("\n"
        "EXOS - Extensible Operating System for %s computers\n"
        "Version %u.%u.%u - Copyright (c) 1999-2025 Jango73\n"),
        Text_Architecture,
        EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR, EXOS_VERSION_PATCH
        );
*/

    SetConsoleBackColor(0);
}

/************************************************************************/

void KernelObjectDestructor(LPVOID Object) {
    SAFE_USE_VALID(Object) {
        LPLISTNODE Node = (LPLISTNODE)Object;
        switch (Node->TypeID) {
        case KOID_MUTEX: DeleteMutex((LPMUTEX)Node);
        }
    }
}

/************************************************************************/

/**
 * @brief Create a kernel object with standard LISTNODE_FIELDS initialization.
 *
 * This function allocates memory for a kernel object and initializes its
 * LISTNODE_FIELDS with the specified ID, References = 1, current process
 * as parent, and NULL for Next/Prev pointers.
 *
 * @param Size Size of the object to allocate (e.g., sizeof(TASK))
 * @param ObjectTypeID ID from ID.h to identify the object type
 * @return Pointer to the allocated and initialized object, or NULL on failure
 */
LPVOID CreateKernelObject(UINT Size, U32 ObjectTypeID) {
    LPLISTNODE Object;
    U8 Identifier[UUID_BINARY_SIZE];
    U64 ObjectID = U64_0;

    DEBUG(TEXT("[CreateKernelObject] Creating object of size %u with ID %x"), Size, ObjectTypeID);

    Object = (LPLISTNODE)KernelHeapAlloc(Size);

    if (Object == NULL) {
        ERROR(TEXT("[CreateKernelObject] Failed to allocate memory for object type %d"), ObjectTypeID);
        return NULL;
    }

    // Initialize LISTNODE_FIELDS
    UUID_Generate(Identifier);
    ObjectID = UUID_ToU64(Identifier);

    Object->TypeID = ObjectTypeID;
    Object->References = 1;
    Object->OwnerProcess = GetCurrentProcess();
    Object->ID = ObjectID;
    Object->Next = NULL;
    Object->Prev = NULL;
    Object->Parent = NULL;

    DEBUG(TEXT("[CreateKernelObject] Object created at %x, OwnerProcess: %x"), Object, Object->OwnerProcess);

    return Object;
}

/************************************************************************/

/**
 * @brief Destroy a kernel object.
 *
 * This function sets the object's ID to KOID_NONE and frees its memory.
 *
 * @param Object Pointer to the kernel object to destroy
 */
void ReleaseKernelObject(LPVOID Object) {
    LPLISTNODE Node = (LPLISTNODE)Object;

    SAFE_USE(Node) {
        if (Node->References) Node->References--;
    }
}

/************************************************************************/

/**
 * @brief Delete unreferenced kernel objects from all kernel lists.
 *
 * This function traverses all kernel object lists and removes objects
 * with a reference count of 0, setting their ID to KOID_NONE and freeing
 * their memory with KernelHeapFree.
 */
void DeleteUnreferencedObjects(void) {
    U32 DeletedCount = 0;

    // Helper function to process a single list
    auto void ProcessList(LPLIST List, LPCSTR ListName) {
        UNUSED(ListName);   // To avoid warnings in release

        if (List == NULL) return;

        LPLISTNODE Current = (LPLISTNODE)List->First;

        while (Current != NULL) {
            LPLISTNODE Next = (LPLISTNODE)Current->Next;

            // Check if object has no references
            if (Current->References == 0) {
                DEBUG(TEXT("[DeleteUnreferencedObjects] Deleting unreferenced %s object at %x (ID: %x)"), ListName, Current, Current->TypeID);

                // Remove from list first
                ListRemove(List, Current);

                // Mark as deleted and free memory
                Current->TypeID = KOID_NONE;
                KernelHeapFree(Current);

                DeletedCount++;
            }

            Current = Next;
        }
    }

    LockMutex(MUTEX_KERNEL, INFINITY);

    // Process all kernel object lists
    ProcessList(GetDesktopList(), TEXT("Desktop"));
    ProcessList(GetProcessList(), TEXT("Process"));
    ProcessList(GetTaskList(), TEXT("Task"));
    ProcessList(GetMutexList(), TEXT("Mutex"));
    ProcessList(GetDiskList(), TEXT("Disk"));
    ProcessList(GetUsbDeviceList(), TEXT("USBDevice"));
    ProcessList(GetUsbInterfaceList(), TEXT("USBInterface"));
    ProcessList(GetUsbEndpointList(), TEXT("USBEndpoint"));
    ProcessList(GetUsbStorageList(), TEXT("USBStorage"));
    ProcessList(GetPCIDeviceList(), TEXT("PCIDevice"));
    ProcessList(GetNetworkDeviceList(), TEXT("NetworkDevice"));
    ProcessList(GetEventList(), TEXT("KernelEvent"));
    ProcessList(GetFileSystemList(), TEXT("FileSystem"));
    ProcessList(GetFileList(), TEXT("File"));
    ProcessList(GetTCPConnectionList(), TEXT("TCPConnection"));
    ProcessList(GetSocketList(), TEXT("Socket"));

    UnlockMutex(MUTEX_KERNEL);
}

/************************************************************************/

static void ReleaseProcessObjectsFromList(LPPROCESS Process, LPLIST List) {
    SAFE_USE(List) {
        LPLISTNODE Node = List->First;

        while (Node != NULL) {
            LPLISTNODE NextNode = Node->Next;

            SAFE_USE_VALID(Node) {
                if (Node->OwnerProcess == Process) {
                    DEBUG(TEXT("[ReleaseProcessKernelObjects] Releasing object %p (ID=%x) owned by %s"),
                          Node,
                          Node->TypeID,
                          Process->FileName);

                    ReleaseKernelObject(Node);
                }
            }

            Node = NextNode;
        }
    }
}

/************************************************************************/

/**
 * @brief Releases references held by a process on all kernel lists.
 *
 * Iterates every kernel-maintained list defined in KernelData.c and calls
 * ReleaseKernelObject() for each object owned by the specified process.
 * The caller must hold MUTEX_KERNEL to ensure list consistency while this
 * routine walks the structures.
 *
 * @param Process Process whose owned kernel objects must be released.
 */
void ReleaseProcessKernelObjects(struct tag_PROCESS* Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process == &KernelProcess) {
            return;
        }

        // Process all kernel object lists
        ReleaseProcessObjectsFromList(Process, GetDesktopList());
        ReleaseProcessObjectsFromList(Process, GetProcessList());
        ReleaseProcessObjectsFromList(Process, GetTaskList());
        ReleaseProcessObjectsFromList(Process, GetMutexList());
        ReleaseProcessObjectsFromList(Process, GetDiskList());
        ReleaseProcessObjectsFromList(Process, GetUsbDeviceList());
        ReleaseProcessObjectsFromList(Process, GetUsbStorageList());
        ReleaseProcessObjectsFromList(Process, GetPCIDeviceList());
        ReleaseProcessObjectsFromList(Process, GetNetworkDeviceList());
        ReleaseProcessObjectsFromList(Process, GetEventList());
        ReleaseProcessObjectsFromList(Process, GetFileSystemList());
        ReleaseProcessObjectsFromList(Process, GetFileList());
        ReleaseProcessObjectsFromList(Process, GetTCPConnectionList());
        ReleaseProcessObjectsFromList(Process, GetSocketList());
    }
}

/************************************************************************/

/**
 * @brief Store object termination state in cache.
 * @param Object Handle of the terminated object
 * @param ExitCode Exit code of the object
 */
void StoreObjectTerminationState(LPVOID Object, UINT ExitCode) {
    LPOBJECT KernelObject = (LPOBJECT)Object;

    SAFE_USE_VALID(KernelObject) {
        LPOBJECT_TERMINATION_STATE TermState = (LPOBJECT_TERMINATION_STATE)
            KernelHeapAlloc(sizeof(OBJECT_TERMINATION_STATE));

        SAFE_USE(TermState) {
            U32 IdHigh = U64_High32(KernelObject->ID);
            U32 IdLow = U64_Low32(KernelObject->ID);

            TermState->Object = KernelObject;
            TermState->ExitCode = ExitCode;
            TermState->ID = KernelObject->ID;
            CacheAdd(GetObjectTerminationCache(), TermState, OBJECT_TERMINATION_TTL_MS);

            DEBUG(TEXT("[StoreObjectTerminationState] Handle=%x ID=%08x%08x ExitCode=%u"),
                  KernelObject,
                  IdHigh,
                  IdLow,
                  ExitCode);

            UNUSED(IdHigh);
            UNUSED(IdLow);
        }

        return;
    }

    WARNING(TEXT("[StoreObjectTerminationState] Invalid kernel object pointer %x"), Object);
}

/************************************************************************/

/**
 * @brief Selects keyboard layout based on configuration.
 *
 * Reads the layout from the parsed configuration and applies it with
 * SelectKeyboard.
 */

static void UseConfiguration(void) {
    DEBUG(TEXT("[UseConfiguration] Enter"));

    LPTOML Configuration = GetConfiguration();

    SAFE_USE(Configuration) {
        LPCSTR Layout;
        LPCSTR QuantumMS;
        LPCSTR DoLogin;

        DEBUG(TEXT("[UseConfiguration] Handling keyboard layout"));

        Layout = TomlGet(Configuration, TEXT("Keyboard.Layout"));

        if (Layout) {
            SelectKeyboard(Layout);
        } else {
            ConsolePrint(TEXT("Keyboard layout not found in config, using default en-US\n"));
            SelectKeyboard(TEXT("en-US"));
        }

        QuantumMS = TomlGet(Configuration, TEXT(CONFIG_GENERAL_QUANTUM_MS));

        if (STRING_EMPTY(QuantumMS) == FALSE) {
            DEBUG(TEXT("Task quantum set to %s\n"), QuantumMS);
            SetMinimumQuantum(StringToU32(QuantumMS));
        }

        DoLogin = TomlGet(Configuration, TEXT("General.DoLogin"));

        if (STRING_EMPTY(DoLogin) == FALSE) {
            SetDoLogin((StringToU32(DoLogin) != 0));
        } else {
            SetDoLogin(TRUE);
        }

        if (GetDoLogin() == FALSE) {
            ConsolePrint(TEXT("WARNING : Login sequence disabled\n"));
        }
    }

    // Ensure a keyboard layout is always set, even if configuration failed
    if (StringEmpty(GetKeyboardCode())) {
        SelectKeyboard(TEXT("en-US"));
    }

#if DEBUG_OUTPUT == 1 || SCHEDULING_DEBUG_OUTPUT == 1
    ConsolePrint(TEXT("WARNING : This is a debug build\n\n"));
#endif

    DEBUG(TEXT("[UseConfiguration] Exit"));
}

/************************************************************************/

/**
 * @brief Calculates the amount of physical memory currently in use.
 *
 * Traverses the page bitmap and counts allocated pages.
 *
 * @return Number of bytes of physical memory used.
 */

U32 GetPhysicalMemoryUsed(void) {
    U32 NumPages = 0;
    U32 Index = 0;
    U32 Byte = 0;
    U32 Mask = 0;
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();

    LockMutex(MUTEX_MEMORY, INFINITY);

    for (Index = 0; Bitmap != NULL && Index < KernelStartup.PageCount; Index++) {
        Byte = Index >> MUL_8;
        Mask = (U32)0x01 << (Index & 0x07);
        if (Bitmap[Byte] & Mask) NumPages++;
    }

    UnlockMutex(MUTEX_MEMORY);

    return (NumPages << PAGE_SIZE_MUL);
}

/************************************************************************/

/**
 * @brief Loads a driver and performs basic validation.
 *
 * Logs the driver address, verifies the magic ID and invokes its load
 * command.
 *
 * @param Driver Pointer to driver structure.
 */

void LoadDriver(LPDRIVER Driver) {
    SAFE_USE(Driver) {
        DEBUG(TEXT("[LoadDriver] : Loading %s driver at %X"), TEXT(Driver->Product), Driver);

        if (Driver->TypeID != KOID_DRIVER) {
            KernelLogText(
                LOG_ERROR, TEXT("%s driver not valid (at address %X). ID = %X. Halting."), TEXT(Driver->Product), Driver, Driver->TypeID);

            // Wait forever
            DO_THE_SLEEPING_BEAUTY;
        }

        UINT Result = Driver->Command(DF_LOAD, 0);
        if (Result == DF_RETURN_SUCCESS && (Driver->Flags & DRIVER_FLAG_READY) != 0) {
            DEBUG(TEXT("[LoadDriver] : %s driver loaded successfully"), TEXT(Driver->Product));
            TEST(TEXT("[LoadDriver] %s.Load : OK"), TEXT(Driver->Product));
        } else {
            TEST(TEXT("[LoadDriver] %s.Load : KO"), TEXT(Driver->Product));
            if ((Driver->Flags & DRIVER_FLAG_CRITICAL)) {
                ConsolePanic(TEXT("Critical driver %s failed to load"), TEXT(Driver->Product));
            } else {
                ERROR(TEXT("[LoadDriver] : Failed to load %s driver (code = %x)"), TEXT(Driver->Product), Result);
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Unload a driver by invoking its DF_UNLOAD command.
 *
 * Logs the unload attempt, validates the driver descriptor and reports
 * failures while allowing shutdown to continue.
 *
 * @param Driver Pointer to driver structure.
 */
void UnloadDriver(LPDRIVER Driver) {
    SAFE_USE(Driver) {
        DEBUG(TEXT("[UnloadDriver] : Unloading %s driver at %X"), TEXT(Driver->Product), Driver);

        if (Driver->TypeID != KOID_DRIVER) {
            WARNING(TEXT("[UnloadDriver] : %s driver not valid (at address %X). ID = %X."), TEXT(Driver->Product), Driver, Driver->TypeID);
            return;
        }

        UINT Result = Driver->Command(DF_UNLOAD, 0);
        if (Result == DF_RETURN_SUCCESS) {
            DEBUG(TEXT("[UnloadDriver] : %s driver unloaded successfully"), TEXT(Driver->Product));
            TEST(TEXT("[UnloadDriver] %s.Unload : OK"), TEXT(Driver->Product));
        } else {
            TEST(TEXT("[UnloadDriver] %s.Unload : KO"), TEXT(Driver->Product));
            WARNING(TEXT("[UnloadDriver] : Failed to unload %s driver (code = %x)"), TEXT(Driver->Product), Result);
        }
    }
}

/************************************************************************/

void LoadAllDrivers(void) {
    InitializeDriverList();

    LPLIST DriverList = GetDriverList();
    if (DriverList == NULL || DriverList->First == NULL) {
        return;
    }

    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
        LoadDriver((LPDRIVER)Node);
    }
}

/************************************************************************/

/**
 * @brief Unloads all drivers in reverse initialization order.
 *
 * Walks the driver list from tail to head and dispatches DF_UNLOAD to
 * each registered driver.
 */
void UnloadAllDrivers(void) {
    LPLIST DriverList = GetDriverList();
    if (DriverList == NULL || DriverList->Last == NULL) {
        return;
    }

    for (LPLISTNODE Node = DriverList->Last; Node; Node = Node->Prev) {
        UnloadDriver((LPDRIVER)Node);
    }
}

/************************************************************************/

static void KillActiveUserlandProcesses(void);
static void KillActiveKernelTasks(void);

/**
 * @brief Common pre-shutdown sequence used by power actions.
 *
 * Kills active userland processes, then kernel tasks, then unloads all
 * drivers in reverse initialization order.
 */
static void PrepareForPowerTransition(void) {
    DEBUG(TEXT("[PrepareForPowerTransition] Killing userland processes"));
    KillActiveUserlandProcesses();

    DEBUG(TEXT("[PrepareForPowerTransition] Killing kernel tasks"));
    KillActiveKernelTasks();

    DEBUG(TEXT("[PrepareForPowerTransition] Unloading drivers"));
    UnloadAllDrivers();
}

/************************************************************************/

static U32 KernelMonitor(LPVOID Parameter) {
    UNUSED(Parameter);
    U32 LogCounter = 0;

    FINE_DEBUG("[KernelMonitor] Enter");

    FOREVER {
        DeleteDeadTasksAndProcesses();
        DeleteUnreferencedObjects();
        CacheCleanup(GetObjectTerminationCache(), GetSystemTime());

        LogCounter++;
        if (LogCounter >= 60) {  // 60 * 500ms = 30 seconds
            DEBUG(TEXT("[KernelMonitor] Monitor task running normally"));
            LogCounter = 0;
        }

        Sleep(500);
    }

    return 0;
}

/************************************************************************/

void KernelIdle(void) {
    ConsoleSetPagingActive(TRUE);

    FOREVER {
        Sleep(4000);
    }
}

/************************************************************************/

/**
 * @brief Terminates all userland processes without signaling.
 *
 * Collects active ring-3 processes and immediately kills them. This
 * routine builds a temporary list under MUTEX_PROCESS to avoid holding
 * the lock while issuing KillProcess().
 */
static void KillActiveUserlandProcesses(void) {
    LPLIST ProcessesToKill = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (ProcessesToKill == NULL) {
        WARNING(TEXT("[KillActiveUserlandProcesses] Unable to allocate kill list"));
        return;
    }

    LockMutex(MUTEX_PROCESS, INFINITY);

    LPLIST ProcessList = GetProcessList();
    SAFE_USE(ProcessList) {
        for (LPPROCESS Process = (LPPROCESS)ProcessList->First; Process; Process = (LPPROCESS)Process->Next) {
            SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
                if (Process != &KernelProcess && Process->Privilege == CPU_PRIVILEGE_USER &&
                    Process->Status != PROCESS_STATUS_DEAD) {
                    ListAddTail(ProcessesToKill, Process);
                }
            }
        }
    }

    UnlockMutex(MUTEX_PROCESS);

    for (LPLISTNODE Node = ProcessesToKill->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            DEBUG(TEXT("[KillActiveUserlandProcesses] Killing process %s"), Process->FileName);
            KillProcess(Process);
        }
    }

    DeleteList(ProcessesToKill);
}

/************************************************************************/

/**
 * @brief Terminates all kernel tasks except the main kernel task.
 *
 * Collects tasks attached to the kernel process and flags them dead
 * through KillTask(). The main kernel task is left running.
 */
static void KillActiveKernelTasks(void) {
    LPLIST TasksToKill = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (TasksToKill == NULL) {
        WARNING(TEXT("[KillActiveKernelTasks] Unable to allocate kill list"));
        return;
    }

    LockMutex(MUTEX_TASK, INFINITY);

    LPLIST TaskList = GetTaskList();
    SAFE_USE(TaskList) {
        for (LPTASK Task = (LPTASK)TaskList->First; Task; Task = (LPTASK)Task->Next) {
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->Process == &KernelProcess && Task->Type != TASK_TYPE_KERNEL_MAIN &&
                    Task->Status != TASK_STATUS_DEAD) {
                    ListAddTail(TasksToKill, Task);
                }
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    for (LPLISTNODE Node = TasksToKill->First; Node; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            DEBUG(TEXT("[KillActiveKernelTasks] Killing task %s"), Task->Name);
            KillTask(Task);
        }
    }

    DeleteList(TasksToKill);
}

/************************************************************************/

/**
 * @brief Entry point for kernel initialization.
 *
 * Sets up core services, loads drivers, mounts file systems and starts
 * the initial shell task.
 *
 */

void InitializeKernel(void) {
    TASKINFO TaskInfo;

    GetCPUInformation(GetKernelCPUInfo());
    PreInitializeKernel();

    //-------------------------------------
    // Load all drivers

    LoadAllDrivers();

    //-------------------------------------
    // Initialize stuff

    CacheInit(GetObjectTerminationCache(), CACHE_DEFAULT_CAPACITY);

    HandleMapInit(GetHandleMap());

    InitializeFocusState();

    InitializeQuantumTime();

    //-------------------------------------
    // Set configuration dependent stuff

    UseConfiguration();

    //-------------------------------------
    // Run auto tests

    RunAllTests();

    //-------------------------------------
    // Print the EXOS banner

    Welcome();

    DEBUG(TEXT("[InitializeKernel] Welcome done"));

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();

    DEBUG(TEXT("[InitializeKernel] Interrupts enabled"));

    //-------------------------------------

    LPTOML Configuration = GetConfiguration();
    LPCSTR Mono = NULL;

    SAFE_USE(Configuration) { Mono = TomlGet(Configuration, TEXT("General.Mono")); }

    if (STRING_EMPTY(Mono) == FALSE && StringCompare(Mono, TEXT("1")) == 0) {
        Shell(NULL);
    } else {
        //-------------------------------------
        // Kernel monitor

        DEBUG(TEXT("[InitializeKernel] ========================================"));
        DEBUG(TEXT("[InitializeKernel] Starting monitor task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = KernelMonitor;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("KernelMonitor"));

        CreateTask(&KernelProcess, &TaskInfo);

        //-------------------------------------
        // Test tasks

        /*
        DEBUG(TEXT("[InitializeKernel] ========================================"));
        KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Starting task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = ClockTestTask;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_LOWEST;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("ClockTestTask"));

        TaskInfo.Parameter = (LPVOID)(((Console.Width - 8) << 16) | 0);
        CreateTask(&KernelProcess, &TaskInfo);
        */

        //-------------------------------------
        // Shell task

        DEBUG(TEXT("[InitializeKernel] ========================================"));
        DEBUG(TEXT("[InitializeKernel] Starting shell task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = Shell;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("Shell"));

        CreateTask(&KernelProcess, &TaskInfo);
    }

    //--------------------------------------
    // Enter idle

    KernelIdle();
}

/************************************************************************/

/**
 * @brief Performs a clean shutdown then powers off through ACPI.
 *
 * Drivers are unloaded in reverse initialization order before invoking
 * ACPIPowerOff().
 */
void ShutdownKernel(void) {
    DEBUG(TEXT("[ShutdownKernel] Preparing for shutdown"));
    PrepareForPowerTransition();
    ACPIPowerOff();
}

/************************************************************************/

/**
 * @brief Performs a clean shutdown then reboots through ACPI.
 *
 * Drivers are unloaded in reverse initialization order before invoking
 * ACPIReboot().
 */
void RebootKernel(void) {
    DEBUG(TEXT("[RebootKernel] Preparing for reboot"));
    PrepareForPowerTransition();
    ACPIReboot();
}
