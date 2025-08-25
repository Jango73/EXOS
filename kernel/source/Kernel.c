
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"

#include "../include/Clock.h"
#include "../include/Console.h"
#include "../include/Driver.h"
#include "../include/E1000.h"
#include "../include/FileSys.h"
#include "../include/HD.h"
#include "../include/Interrupt.h"
#include "../include/Keyboard.h"
#include "../include/Log.h"
#include "../include/Mouse.h"
#include "../include/PCI.h"
#include "../include/System.h"

/***************************************************************************/

extern U32 DeadBeef;

extern void StartTestNetworkTask(void);

/***************************************************************************/

STR Text_OSTitle[] =
    "EXOS - Extensible Operating System - Version 1.00\n"
    "Copyright (c) 1999-2025 Jango73.\n"
    "All rights reserved.\n";

/***************************************************************************/

static LIST DesktopList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

/***************************************************************************/

static LIST ProcessList = {
    .First = (LPLISTNODE)&KernelProcess,
    .Last = (LPLISTNODE)&KernelProcess,
    .Current = (LPLISTNODE)&KernelProcess,
    .NumItems = 1,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

/***************************************************************************/

static LIST TaskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 1,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

/***************************************************************************/

static LIST MutexList = {
    .First = (LPLISTNODE)&KernelMutex,
    .Last = (LPLISTNODE)&ConsoleMutex,
    .Current = (LPLISTNODE)&KernelMutex,
    .NumItems = 9,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

/***************************************************************************/

static LIST DiskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

static LIST PciDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

static LIST FileSystemList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

static LIST FileList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

/***************************************************************************/

KERNELDATA_I386 Kernel_i386 = {
    .GDT = 0,
    .TTD = 0,
    .TSS = 0,
    .PPB = (U8*) 1                       // To force inclusion in .data
};

KERNELDATA Kernel = {
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .PCIDevice = &PciDeviceList,
    .FileSystem = &FileSystemList,
    .File = &FileList,
    .CPU = {.Name = "", .Type = 0, .Family = 0, .Model = 0, .Stepping = 0, .Features = 0}};

/***************************************************************************/

LPVOID KernelMemAlloc(U32 Size) { return HeapAlloc_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, Size); }

/***************************************************************************/

void KernelMemFree(LPVOID Pointer) { HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, Pointer); }

/***************************************************************************/

void CheckDataIntegrity(void) {
    if (DeadBeef != 0xDEADBEEF) {
        KernelLogText(LOG_DEBUG, TEXT("Expected a dead beef at %X"), (&DeadBeef));
        KernelLogText(LOG_DEBUG, TEXT("Data corrupt, halting"));

        // Wait forever
        DO_THE_SLEEPING_BEAUTY;
    }
}

/***************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

/***************************************************************************/

BOOL GetCPUInformation(LPCPUINFORMATION Info) {
    CPUIDREGISTERS Regs[4];

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

/***************************************************************************/

U32 ClockTask(LPVOID Param) {
    STR Text[64];
    U32 X = ((U32)Param & 0xFFFF0000) >> 16;
    U32 Y = ((U32)Param & 0x0000FFFF) >> 0;
    U32 OldX = 0;
    U32 OldY = 0;
    I32 MouseX = 0;
    I32 MouseY = 0;
    U32 Buttons = 0;

    U32 Time = 0;
    U32 OldTime = 0;

    while (1) {
        Time = DoSystemCall(SYSCALL_GetSystemTime, 0);

        if (Time - OldTime >= 1000) {
            OldTime = Time;
            MilliSecondsToHMS(Time, Text);
            OldX = Console.CursorX;
            OldY = Console.CursorY;

            Console.CursorX = X;
            Console.CursorY = Y;
            ConsolePrint(Text);

            KernelLogText(LOG_VERBOSE, Text);

            MouseX = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
            MouseY = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);
            Buttons = SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
            Console.CursorX = 0;
            Console.CursorY = 0;
            ConsolePrint(TEXT("%d %d %d"), MouseX, MouseY, Buttons);

            Console.CursorX = OldX;
            Console.CursorY = OldY;
        }

        DoSystemCall(SYSCALL_Sleep, 40);
    }

    return 0;
}

/***************************************************************************/

void DumpCriticalInformation(void) {
    for (U32 Index = 0; Index < KernelStartup.E820_Count; Index++) {
        KernelLogText(LOG_DEBUG, TEXT("E820 entry %X : %X, %X, %X"),
        Index, (U32)KernelStartup.E820[Index].Base.LO, (U32)KernelStartup.E820[Index].Size.LO, (U32)KernelStartup.E820[Index].Type);
    }

    KernelLogText(LOG_DEBUG, TEXT("Virtual addresses"));
    KernelLogText(LOG_DEBUG, TEXT("LA_RAM : %X"), LA_RAM);
    KernelLogText(LOG_DEBUG, TEXT("LA_VIDEO : %X"), LA_VIDEO);
    KernelLogText(LOG_DEBUG, TEXT("LA_CONSOLE : %X"), LA_CONSOLE);
    KernelLogText(LOG_DEBUG, TEXT("LA_USER : %X"), LA_USER);
    KernelLogText(LOG_DEBUG, TEXT("LA_LIBRARY : %X"), LA_LIBRARY);
    KernelLogText(LOG_DEBUG, TEXT("LA_KERNEL : %X"), LA_KERNEL);

    KernelLogText(LOG_DEBUG, TEXT("Kernel startup info:"));
    KernelLogText(LOG_DEBUG, TEXT("  StubAddress : %X"), KernelStartup.StubAddress);
    KernelLogText(LOG_DEBUG, TEXT("  IRQMask_21_RM : %X"), KernelStartup.IRQMask_21_RM);
    KernelLogText(LOG_DEBUG, TEXT("  IRQMask_A1_RM : %X"), KernelStartup.IRQMask_A1_RM);
    KernelLogText(LOG_DEBUG, TEXT("  MemorySize : %X"), KernelStartup.MemorySize);
    KernelLogText(LOG_DEBUG, TEXT("  PageCount : %X"), KernelStartup.PageCount);
    KernelLogText(LOG_DEBUG, TEXT("  E820 entry count : %X"), KernelStartup.E820_Count);
}

/***************************************************************************/

void DumpSystemInformation(void) {
    KernelLogText(LOG_VERBOSE, TEXT("DumpSystemInformation"));

    //-------------------------------------
    // Print information on computer

    ConsolePrint(TEXT("Computer ID : "));
    ConsolePrint(Kernel.CPU.Name);
    ConsolePrint(Text_NewLine);

    //-------------------------------------
    // Print information on memory

    ConsolePrint(TEXT("Physical memory : %d"), KernelStartup.MemorySize / 1024);
    ConsolePrint(Text_Space);
    ConsolePrint(Text_KB);
    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

void InitializePCI(void) {
    PCI_RegisterDriver(&E1000Driver);
    PCI_ScanBus();
}

/***************************************************************************/

void InitializeFileSystems(void) {
    LPLISTNODE Node;

    MountSystemFS();

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        MountDiskPartitions((LPPHYSICALDISK)Node, NULL, 0);
    }
}

/***************************************************************************/

U32 GetPhysicalMemoryUsed(void) {
    U32 NumPages = 0;
    U32 Index = 0;
    U32 Byte = 0;
    U32 Mask = 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    for (Index = 0; Index < KernelStartup.PageCount; Index++) {
        Byte = Index >> MUL_8;
        Mask = (U32)0x01 << (Index & 0x07);
        if (Kernel_i386.PPB[Byte] & Mask) NumPages++;
    }

    UnlockMutex(MUTEX_MEMORY);

    return (NumPages << PAGE_SIZE_MUL);
}

/***************************************************************************/

void LoadDriver(LPDRIVER Driver, LPCSTR Name) {
    if (Driver != NULL) {
        KernelLogText(LOG_DEBUG, TEXT("[LoadDriver] : %s at %X"), Name, Driver);

        if (Driver->ID != ID_DRIVER) {
            KernelLogText(
                LOG_ERROR, TEXT("%s driver not valid (at address %X). ID = %X. Halting."), Name, Driver, Driver->ID);

            // Wait forever
            DO_THE_SLEEPING_BEAUTY;
        }
        Driver->Command(DF_LOAD, 0);
    }
}

/***************************************************************************/

void InitializeKernel(U32 ImageAddress, U8 CursorX, U8 CursorY) {
    // PROCESSINFO ProcessInfo;
    // TASKINFO TaskInfo;

    //-------------------------------------
    // No more interrupts

    DisableInterrupts();

    //-------------------------------------
    // Gather startup information

    KernelStartup.StubAddress = ImageAddress;
    KernelStartup.PageDirectory = GetPageDirectory();
    KernelStartup.IRQMask_21_RM = 0;
    KernelStartup.IRQMask_A1_RM = 0;
    KernelStartup.ConsoleX = CursorX;
    KernelStartup.ConsoleY = CursorY;
    KernelStartup.MemorySize = N_128MB;
    KernelStartup.PageCount = KernelStartup.MemorySize >> MUL_4KB;
    KernelStartup.E820_Count = 0;

    //-------------------------------------
    // Init the kernel logger

    InitKernelLog();
    KernelLogText(LOG_VERBOSE, TEXT("[KernelMain] Kernel logger initialized"));

    //-------------------------------------
    // Initialize interrupts

    InitializeInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Interrupts initialized"));

    //-------------------------------------
    // Initialize the memory manager

    InitializeMemoryManager();
    KernelLogText(LOG_VERBOSE, TEXT("[KernelMain] Memory manager initialized"));

    InitializeTaskSegments();
    KernelLogText(LOG_VERBOSE, TEXT("[KernelMain] Task segments initialized"));

    //-------------------------------------
    // Check data integrity

    CheckDataIntegrity();

    //-------------------------------------
    // Dump critical information

    DumpCriticalInformation();

    //-------------------------------------
    // Initialize kernel process

    InitializeKernelProcess();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Kernel process and task initialized"));

    //-------------------------------------
    // Initialize the console

    InitializeConsole();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Console initialized"));

    //-------------------------------------
    // Initialize the keyboard

    LoadDriver(&StdKeyboardDriver, TEXT("Keyboard"));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Keyboard initialized"));

    //-------------------------------------
    // Initialize the mouse

    LoadDriver(&SerialMouseDriver, TEXT("SerialMouse"));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Mouse initialized"));

    //-------------------------------------
    // Print system infomation

    DumpSystemInformation();

    //-------------------------------------
    // Get information on CPU

    GetCPUInformation(&(Kernel.CPU));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Got CPU information"));

    //-------------------------------------
    // Initialize RAM drives

    LoadDriver(&RAMDiskDriver, TEXT("RAMDisk"));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] RAM drive initialized"));

    //-------------------------------------
    // Initialize physical drives

    LoadDriver(&StdHardDiskDriver, TEXT("StdHardDisk"));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Physical drives initialized"));

    //-------------------------------------
    // Initialize the file systems

    InitializeFileSystems();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] File systems initialized"));

    //-------------------------------------
    // Initialize the graphics card

    LoadDriver(&VESADriver, TEXT("VESA"));
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] VESA driver initialized"));

    //-------------------------------------
    // Initialize the PCI drivers

    InitializePCI();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] PCI manager initialized"));

    //-------------------------------------
    // Initialize the clock

    InitializeClock();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Clock initialized"));

    //-------------------------------------
    // Print the EXOS banner

    ConsolePrint(Text_OSTitle);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernel] OS title printed"));

    //-------------------------------------
    // Test tasks

/*
    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = ClockTask;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags = 0;

    TaskInfo.Parameter = (LPVOID)(((U32)70 << 16) | 0);
    CreateTask(&KernelProcess, &TaskInfo);
*/
    // StartTestNetworkTask();

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Interrupts enabled"));

    //-------------------------------------
    // Shell task

    /*
    ConsolePrint(TEXT("Launching shell"));

    KernelLogText(LOG_VERBOSE, TEXT("[InitializeKernel] Starting shell"));

    TaskInfo.Func = Shell;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;

    CreateTask(&KernelProcess, &TaskInfo);
    */

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernel] Calling Shell"));

    Shell(NULL);

    //-------------------------------------
    // Launch Portal (windowing system)

    /*
      StringCopy(FileName, "C:/EXOS/SYSTEM/EXPLORER.PRG");

      ProcessInfo.Size        = sizeof ProcessInfo;
      ProcessInfo.Flags       = 0;
      ProcessInfo.FileName    = FileName;
      ProcessInfo.CommandLine = NULL;
      ProcessInfo.StdOut      = NULL;
      ProcessInfo.StdIn       = NULL;
      ProcessInfo.StdErr      = NULL;

      CreateProcess(&ProcessInfo);
    */
}

/***************************************************************************/
