
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
#include "Arch.h"
#include "Process.h"

#include "drivers/ACPI.h"
#include "drivers/LocalAPIC.h"
#include "drivers/IOAPIC.h"
#include "InterruptController.h"
#include "Autotest.h"
#include "Clock.h"
#include "Console.h"
#include "Driver.h"
#include "drivers/E1000.h"
#include "File.h"
#include "FileSystem.h"
#include "drivers/ATA.h"
#include "drivers/SATA.h"
#include "Interrupt.h"
#include "drivers/Keyboard.h"
#include "Lang.h"
#include "Log.h"
#include "Mouse.h"
#include "drivers/PCI.h"
#include "Stack.h"
#include "System.h"
#include "SystemFS.h"
#include "utils/TOML.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "NetworkManager.h"
#include "utils/UUID.h"

/************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

/***************************************************************************/

extern U32 DeadBeef;
extern void StartTestNetworkTask(void);

/************************************************************************/

U32 EXOS_End SECTION(".end_mark") = 0x534F5845;

/************************************************************************/

/**
 * @brief Checks that the DeadBeef sentinel retains its expected value.
 *
 * This routine verifies the global DeadBeef variable and halts if it has
 * been altered, indicating memory corruption.
 */

void CheckDataIntegrity(void) {
    if (DeadBeef != 0xDEADBEEF) {
        DEBUG(TEXT("Expected a dead beef at %X"), (&DeadBeef));
        DEBUG(TEXT("Data corrupt, halting"));

        // Wait forever
        DO_THE_SLEEPING_BEAUTY;
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
 * @brief Initializes the kernel minimum quantum time based on environment.
 *
 * Uses BARE_METAL compile-time flag to determine quantum time.
 * Also accounts for debug output overhead which slows execution.
 * Sets Kernel.MinimumQuantum and Kernel.MaximumQuantum to appropriate value.
 */
void InitializeQuantumTime(void) {
    // Set base quantum time based on environment
#if BARE_METAL == 1
    Kernel.MinimumQuantum = 10;  // Shorter quantum for bare-metal
    DEBUG(TEXT("[InitializeQuantumTime] Bare-metal mode, base quantum = %d ms"), Kernel.MinimumQuantum);
#else
    Kernel.MinimumQuantum = 50;  // Longer quantum for emulation/virtualization
    DEBUG(TEXT("[InitializeQuantumTime] Emulation mode, base quantum = %d ms"), Kernel.MinimumQuantum);
#endif

#if SCHEDULING_DEBUG_OUTPUT == 1
    // Double quantum when scheduling debug is enabled (logs slow down execution)
    Kernel.MinimumQuantum *= 2;
    DEBUG(TEXT("[InitializeQuantumTime] Scheduling debug enabled, final quantum = %d ms"),
        Kernel.MinimumQuantum);
#endif

    Kernel.MaximumQuantum = Kernel.MinimumQuantum * 4;
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
        Time = DoSystemCall(SYSCALL_GetSystemTime, 0);

        if (Time - OldTime >= 1000) {
            OldTime = Time;
            MilliSecondsToHMS(Time, Text);
            OldX = Console.CursorX;
            OldY = Console.CursorY;
            SetConsoleCursorPosition(X, Y);
            ConsolePrint(Text);
            SetConsoleCursorPosition(OldX, OldY);
        }

        DoSystemCall(SYSCALL_Sleep, 500);
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

#if DEBUG_OUTPUT == 1 || SCHEDULING_DEBUG_OUTPUT == 1
    ConsolePrint(TEXT("WARNING : This is a debug build.\n"));
#endif

    //-------------------------------------
    // Print information on memory

    ConsolePrint(TEXT("Physical memory : %lu"), KernelStartup.MemorySize / 1024);
    ConsolePrint(Text_Space);
    ConsolePrint(Text_KB);
    ConsolePrint(Text_NewLine);

    ConsolePrint(
        TEXT("\n"
        "EXOS - Extensible Operating System - Version %u.%u.%u\n"
        "Copyright (c) 1999-2025 Jango73\n"),
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
        TEXT(" - Version %u.%u.%u\n"
        "Copyright (c) 1999-2025 Jango73\n"),
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
    U32 Index;

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

    DEBUG(TEXT("[CreateKernelObject] Object created at %x, OwnerProcess: %x"),
          (U32)Object, (U32)Object->OwnerProcess);

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
                DEBUG(TEXT("[DeleteUnreferencedObjects] Deleting unreferenced %s object at %x (ID: %x)"), ListName, (U32)Current, Current->TypeID);

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
    ProcessList(Kernel.Desktop, TEXT("Desktop"));
    ProcessList(Kernel.Process, TEXT("Process"));
    ProcessList(Kernel.Task, TEXT("Task"));
    ProcessList(Kernel.Mutex, TEXT("Mutex"));
    ProcessList(Kernel.Disk, TEXT("Disk"));
    ProcessList(Kernel.PCIDevice, TEXT("PCIDevice"));
    ProcessList(Kernel.NetworkDevice, TEXT("NetworkDevice"));
    ProcessList(Kernel.FileSystem, TEXT("FileSystem"));
    ProcessList(Kernel.File, TEXT("File"));
    ProcessList(Kernel.TCPConnection, TEXT("TCPConnection"));
    ProcessList(Kernel.Socket, TEXT("Socket"));

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
        ReleaseProcessObjectsFromList(Process, Kernel.Desktop);
        ReleaseProcessObjectsFromList(Process, Kernel.Process);
        ReleaseProcessObjectsFromList(Process, Kernel.Task);
        ReleaseProcessObjectsFromList(Process, Kernel.Mutex);
        ReleaseProcessObjectsFromList(Process, Kernel.Disk);
        ReleaseProcessObjectsFromList(Process, Kernel.PCIDevice);
        ReleaseProcessObjectsFromList(Process, Kernel.NetworkDevice);
        ReleaseProcessObjectsFromList(Process, Kernel.FileSystem);
        ReleaseProcessObjectsFromList(Process, Kernel.File);
        ReleaseProcessObjectsFromList(Process, Kernel.TCPConnection);
        ReleaseProcessObjectsFromList(Process, Kernel.Socket);
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
            CacheAdd(&Kernel.ObjectTerminationCache, TermState, OBJECT_TERMINATION_TTL_MS);

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
 * @brief Loads and parses the kernel configuration file.
 *
 * Attempts to read "exos.toml" (case insensitive) and stores the resulting
 * TOML data in Kernel.Configuration.
 */

static void ReadKernelConfiguration(void) {
    DEBUG(TEXT("[ReadKernelConfiguration] Enter"));

    U32 Size = 0;
    LPVOID Buffer = FileReadAll(TEXT("exos.toml"), &Size);

    if (Buffer == NULL) {
        Buffer = FileReadAll(TEXT("EXOS.TOML"), &Size);

        SAFE_USE(Buffer) {
            DEBUG(TEXT("[ReadKernelConfiguration] Config read from EXOS.TOML"));
        }
    } else {
        DEBUG(TEXT("[ReadKernelConfiguration] Config read from exos.toml"));
    }

    SAFE_USE(Buffer) {
        Kernel.Configuration = TomlParse((LPCSTR)Buffer);
        KernelHeapFree(Buffer);
    }

    DEBUG(TEXT("[ReadKernelConfiguration] Exit"));
}

/************************************************************************/

/**
 * @brief Selects keyboard layout based on configuration.
 *
 * Reads the layout from Kernel.Configuration and applies it with
 * SelectKeyboard.
 */

static void UseConfiguration(void) {
    DEBUG(TEXT("[UseConfiguration] Enter"));

    SAFE_USE(Kernel.Configuration) {
        LPCSTR Layout;
        LPCSTR Quantum;
        LPCSTR DoLogin;

        DEBUG(TEXT("[UseConfiguration] Handling keyboard layout"));

        Layout = TomlGet(Kernel.Configuration, TEXT("Keyboard.Layout"));

        if (Layout) {
            ConsolePrint(TEXT("Keyboard = %s\n"), Layout);
            SelectKeyboard(Layout);
        } else {
            ConsolePrint(TEXT("Keyboard layout not found in config, using default en-US\n"));
            SelectKeyboard(TEXT("en-US"));
        }

        Quantum = TomlGet(Kernel.Configuration, TEXT("General.Quantum"));

        if (STRING_EMPTY(Quantum) == FALSE) {
            ConsolePrint(TEXT("Task quantum set to %s\n"), Quantum);
            Kernel.MinimumQuantum = StringToU32(Quantum);
        }

        DoLogin = TomlGet(Kernel.Configuration, TEXT("General.DoLogin"));

        if (STRING_EMPTY(DoLogin) == FALSE) {
            Kernel.DoLogin = (StringToU32(DoLogin) != 0);
            ConsolePrint(TEXT("Login sequence %s\n"), Kernel.DoLogin ? TEXT("enabled") : TEXT("disabled"));
        } else {
            Kernel.DoLogin = TRUE;
            ConsolePrint(TEXT("DoLogin not found in config, login sequence enabled by default\n"));
        }
    }

    // Ensure a keyboard layout is always set, even if configuration failed
    if (StringEmpty(Kernel.KeyboardCode)) {
        SelectKeyboard(TEXT("en-US"));
    }

    DEBUG(TEXT("[UseConfiguration] Exit"));
}

/************************************************************************/

/**
 * @brief Initializes PCI subsystem by registering drivers and scanning the bus.
 */

static void InitializePCI(void) {
    extern PCI_DRIVER AHCIPCIDriver;

    PCI_RegisterDriver(&E1000Driver);
    PCI_RegisterDriver(&AHCIPCIDriver);
    PCI_ScanBus();
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

    LockMutex(MUTEX_MEMORY, INFINITY);

    for (Index = 0; Index < KernelStartup.PageCount; Index++) {
        Byte = Index >> MUL_8;
        Mask = (U32)0x01 << (Index & 0x07);
        if (Kernel.PPB[Byte] & Mask) NumPages++;
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
 * @param Name   Driver name for logging.
 */

void LoadDriver(LPDRIVER Driver, LPCSTR Name) {
    SAFE_USE(Driver) {
        DEBUG(TEXT("[LoadDriver] : Loading %s driver at %X"), Name, Driver);

        if (Driver->TypeID != KOID_DRIVER) {
            KernelLogText(
                LOG_ERROR, TEXT("%s driver not valid (at address %X). ID = %X. Halting."), Name, Driver, Driver->TypeID);

            // Wait forever
            DO_THE_SLEEPING_BEAUTY;
        }
        Driver->Command(DF_LOAD, 0);
    }
}

/************************************************************************/

static U32 MonitorKernel(LPVOID Parameter) {
    UNUSED(Parameter);
    U32 LogCounter = 0;

    FOREVER {
        DeleteDeadTasksAndProcesses();
        DeleteUnreferencedObjects();
        CacheCleanup(&Kernel.ObjectTerminationCache, GetSystemTime());

        LogCounter++;
        if (LogCounter >= 10) {  // 10 * 500ms = 5 seconds
            DEBUG("[MonitorKernel] Monitor task running normally");
            LogCounter = 0;
        }

        Sleep(500);
    }

    return 0;
}

/************************************************************************/

void KernelIdle(void) {
    FOREVER {
        Sleep(4000);
    }
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

    ArchPreInitializeKernel();

    //-------------------------------------
    // Gather startup information

    KernelStartup.PageDirectory = GetPageDirectory();
    KernelStartup.IRQMask_21_RM = 0;
    KernelStartup.IRQMask_A1_RM = 0;

    //-------------------------------------
    // Initialize the console

    InitializeConsole();

    //-------------------------------------
    // Init the kernel logger

    InitKernelLog();

    DEBUG(TEXT("[InitializeKernel] Kernel logger initialized"));

    //-------------------------------------
    // Check RealModeCall memory pages validity

    DEBUG(TEXT("[InitializeKernel] Register integer size : %d"), sizeof(UINT));
    DEBUG(TEXT("[InitializeKernel] Console cursor : %d, %d"), Console.CursorX, Console.CursorY);
    DEBUG(TEXT("[InitializeKernel] GDT base address read: %p"), Kernel_i386.GDT);
    DEBUG(TEXT("[InitializeKernel] LOW_MEMORY_PAGE_1 (%p) valid: %d"), LOW_MEMORY_PAGE_1, IsValidMemory(LOW_MEMORY_PAGE_1));
    DEBUG(TEXT("[InitializeKernel] LOW_MEMORY_PAGE_2 (%p) valid: %d"), LOW_MEMORY_PAGE_2, IsValidMemory(LOW_MEMORY_PAGE_2));
    DEBUG(TEXT("[InitializeKernel] LOW_MEMORY_PAGE_3 (%p) valid: %d"), LOW_MEMORY_PAGE_3, IsValidMemory(LOW_MEMORY_PAGE_3));
    DEBUG(TEXT("[InitializeKernel] LOW_MEMORY_PAGE_5 (%p) valid: %d"), LOW_MEMORY_PAGE_5, IsValidMemory(LOW_MEMORY_PAGE_5));
    DEBUG(TEXT("[InitializeKernel] LOW_MEMORY_PAGE_6 (%p) valid: %d"), LOW_MEMORY_PAGE_6, IsValidMemory(LOW_MEMORY_PAGE_6));

    //-------------------------------------
    // Initialize the memory manager

    InitializeMemoryManager();

    DEBUG(TEXT("[KernelMain] Memory manager initialized"));

#if defined(__EXOS_ARCH_I386__)
    InitializeTaskSegments();

    DEBUG(TEXT("[KernelMain] Task segments initialized"));
#endif

    //-------------------------------------
    // Check data integrity

    CheckDataIntegrity();

    //-------------------------------------
    // Initialize interrupts

    InitializeInterrupts();

    DEBUG(TEXT("[InitializeKernel] Interrupts initialized"));

    //-------------------------------------
    // Initialize ACPI

    if (InitializeACPI()) {
        DEBUG(TEXT("[InitializeKernel] ACPI initialized successfully"));
    } else {
        DEBUG(TEXT("[InitializeKernel] ACPI not available or failed to initialize"));
    }

    //-------------------------------------
    // Initialize Local APIC

    if (InitializeLocalAPIC()) {
        DEBUG(TEXT("[InitializeKernel] Local APIC initialized successfully"));
    } else {
        DEBUG(TEXT("[InitializeKernel] Local APIC not available or failed to initialize"));
    }

    //-------------------------------------
    // Initialize I/O APIC

    if (InitializeIOAPIC()) {
        DEBUG(TEXT("[InitializeKernel] I/O APIC initialized successfully"));
    } else {
        DEBUG(TEXT("[InitializeKernel] I/O APIC not available or failed to initialize"));
    }

    //-------------------------------------
    // Initialize Interrupt Controller abstraction layer (after IOAPIC)

    if (InitializeInterruptController(INTCTRL_MODE_AUTO)) {
        DEBUG(TEXT("[InitializeKernel] Interrupt Controller initialized successfully"));
    } else {
        DEBUG(TEXT("[InitializeKernel] Interrupt Controller initialization failed"));
    }

    //-------------------------------------
    // Dump critical information

    DumpCriticalInformation();

    //-------------------------------------
    // Initialize kernel process

    InitializeKernelProcess();

    DEBUG(TEXT("[InitializeKernel] Kernel process and task initialized"));

    //-------------------------------------
    // Initialize object termination cache

    CacheInit(&Kernel.ObjectTerminationCache, CACHE_DEFAULT_CAPACITY);

    DEBUG(TEXT("[InitializeKernel] Object termination cache initialized"));

    //-------------------------------------
    // Run auto tests

    RunAllTests();

    //-------------------------------------
    // Initialize the keyboard

    LoadDriver(&StdKeyboardDriver, TEXT("Keyboard"));

    DEBUG(TEXT("[InitializeKernel] Keyboard initialized"));

    //-------------------------------------
    // Initialize the mouse

    LoadDriver(&SerialMouseDriver, TEXT("SerialMouse"));

    DEBUG(TEXT("[InitializeKernel] Mouse initialized"));

    //-------------------------------------
    // Initialize the clock

    InitializeClock();

    DEBUG(TEXT("[InitializeKernel] Clock initialized"));

    //-------------------------------------
    // Get information on CPU

    GetCPUInformation(&(Kernel.CPU));

    DEBUG(TEXT("[InitializeKernel] Got CPU information"));

    //-------------------------------------
    // Initialize quantum time based on environment and debug settings

    InitializeQuantumTime();

    //-------------------------------------
    // Initialize the PCI drivers

    InitializePCI();

    DEBUG(TEXT("[InitializeKernel] PCI manager initialized"));

    //-------------------------------------
    // Initialize physical drives

    LoadDriver(&ATADiskDriver, TEXT("ATADisk"));
    LoadDriver(&SATADiskDriver, TEXT("SATADisk"));

    DEBUG(TEXT("[InitializeKernel] Physical drives initialized"));

    //-------------------------------------
    // Initialize RAM drives

    LoadDriver(&RAMDiskDriver, TEXT("RAMDisk"));

    DEBUG(TEXT("[InitializeKernel] RAM drive initialized"));

    //-------------------------------------
    // Initialize the file systems

    InitializeFileSystems();

    DEBUG(TEXT("[InitializeKernel] File systems initialized"));

    //-------------------------------------
    // Read kernel configuration

    ReadKernelConfiguration();

    //-------------------------------------
    // Initialize network stack

    InitializeNetwork();

    DEBUG(TEXT("[InitializeKernel] Network manager initialized"));

    //-------------------------------------
    // Mount system folders

    MountUserNodes();

    //-------------------------------------
    // Initialize user account system

    InitializeUserSystem();

    DEBUG(TEXT("[InitializeKernel] User account & session systems initialized"));

    //-------------------------------------
    // Initialize the graphics card

    LoadDriver(&VESADriver, TEXT("VESA"));

    DEBUG(TEXT("[InitializeKernel] VESA driver initialized"));

    //-------------------------------------
    // Set keyboard mapping

    UseConfiguration();

    //-------------------------------------
    // Print the EXOS banner

    Welcome();

    DEBUG(TEXT("[InitializeKernel] Welcome done"));

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();

    DEBUG(TEXT("[InitializeKernel] Interrupts enabled"));

    //-------------------------------------

    LPCSTR Mono = TomlGet(Kernel.Configuration, TEXT("General.Mono"));

    if (StringCompare(Mono, TEXT("1")) == 0) {
        Shell(NULL);
    } else {
        //-------------------------------------
        // Kernel monitor

        DEBUG(TEXT("[InitializeKernel] ========================================"));
        DEBUG(TEXT("[InitializeKernel] Starting monitor task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = MonitorKernel;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("MonitorKernel"));

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
        TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_LOWEST;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("ClockTestTask"));

        TaskInfo.Parameter = (LPVOID)(((Console.Width - 8) << 16) | 0);
        CreateTask(&KernelProcess, &TaskInfo);
        */

        // StartTestNetworkTask();

        //-------------------------------------
        // Network manager task

        DEBUG(TEXT("[InitializeKernel] ========================================"));
        DEBUG(TEXT("[InitializeKernel] Starting network manager task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = NetworkManagerTask;
        TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_LOWER;
        TaskInfo.Flags = 0;
        TaskInfo.Parameter = NULL;
        StringCopy(TaskInfo.Name, TEXT("NetworkManager"));

        CreateTask(&KernelProcess, &TaskInfo);

        //-------------------------------------
        // Shell task

        DEBUG(TEXT("[InitializeKernel] ========================================"));
        DEBUG(TEXT("[InitializeKernel] Starting shell task"));

        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = Shell;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("Shell"));

        CreateTask(&KernelProcess, &TaskInfo);
    }

    //--------------------------------------
    // Enter idle

    KernelIdle();
}
