
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"

#include "../include/Clock.h"
#include "../include/Console.h"
#include "../include/Driver.h"
#include "../include/FileSys.h"
#include "../include/HD.h"
#include "../include/Interrupt.h"
#include "../include/Keyboard.h"
#include "../include/Log.h"
#include "../include/Mouse.h"
#include "../include/System.h"

/***************************************************************************/

extern LINEAR __bss_init_start;
extern LINEAR __bss_init_end;

/***************************************************************************/

STR Text_OSTitle[] =
    "EXOS - Extensible Operating System - Version 1.00\n"
    "Copyright (c) 1999-2025 Jango73.\n"
    "All rights reserved.\n";

/***************************************************************************/

PHYSICAL StubAddress = 1;
KERNELSTARTUPINFO KernelStartup = {
    .Loader_SS = 1,
};

LPGATEDESCRIPTOR IDT = (LPGATEDESCRIPTOR)LA_IDT;
LPSEGMENTDESCRIPTOR GDT = (LPSEGMENTDESCRIPTOR)LA_GDT;
LPTASKTSSDESCRIPTOR TTD = (LPTASKTSSDESCRIPTOR)LA_GDT_TASK;
LPTASKSTATESEGMENT TSS = (LPTASKSTATESEGMENT)LA_TSS;
LPPAGEBITMAP PPB = (LPPAGEBITMAP)LA_PPB;

/***************************************************************************/

static LIST DesktopList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

/***************************************************************************/

static LIST ProcessList = {
    .First = (LPLISTNODE)&KernelProcess,
    .Last = (LPLISTNODE)&KernelProcess,
    .Current = (LPLISTNODE)&KernelProcess,
    .NumItems = 1,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

/***************************************************************************/

static LIST TaskList = {
    .First = (LPLISTNODE)&KernelTask,
    .Last = (LPLISTNODE)&KernelTask,
    .Current = (LPLISTNODE)&KernelTask,
    .NumItems = 1,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

/***************************************************************************/

static LIST MutexList = {
    .First = (LPLISTNODE)&KernelMutex,
    .Last = (LPLISTNODE)&ConsoleMutex,
    .Current = (LPLISTNODE)&KernelMutex,
    .NumItems = 9,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

/***************************************************************************/

static LIST DiskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

static LIST FileSystemList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

static LIST FileList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

/***************************************************************************/

KERNELDATA Kernel = {
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .FileSystem = &FileSystemList,
    .File = &FileList,
    .CPU = {
        .Name = "",
        .Type = 0,
        .Family = 0,
        .Model = 0,
        .Stepping = 0,
        .Features = 0
    }
};

/***************************************************************************/

LPVOID KernelMemAlloc(U32 Size) {
    return HeapAlloc_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, Size);
}

/***************************************************************************/

void KernelMemFree(LPVOID Pointer) {
    return HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize,
                         Pointer);
}

/***************************************************************************/

void SetGateDescriptorOffset(LPGATEDESCRIPTOR This, U32 Offset) {
    This->Offset_00_15 = (Offset & (U32)0x0000FFFF) >> 0x00;
    This->Offset_16_31 = (Offset & (U32)0xFFFF0000) >> 0x10;
}

/***************************************************************************/

BOOL GetSegmentInfo(LPSEGMENTDESCRIPTOR This, LPSEGMENTINFO Info) {
    if (Info) {
        Info->Base = SEGMENTBASE(This);
        Info->Limit = SEGMENTLIMIT(This);
        Info->Type = This->Type;
        Info->Privilege = This->Privilege;
        Info->Granularity = SEGMENTGRANULAR(This);
        Info->CanWrite = This->CanWrite;
        Info->OperandSize = This->OperandSize ? 32 : 16;
        Info->Conforming = This->ConformExpand;
        Info->Present = This->Present;

        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

U32 SegmentInfoToString(LPSEGMENTINFO This, LPSTR Text) {
    if (This && Text) {
        STR Temp[64];

        Text[0] = STR_NULL;

        StringConcat(Text, TEXT("Segment"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Base           : "));
        U32ToHexString(This->Base, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Limit          : "));
        U32ToHexString(This->Limit, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Type           : "));
        StringConcat(Text, This->Type ? TEXT("Code") : TEXT("Data"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Privilege      : "));
        U32ToHexString(This->Privilege, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Granularity    : "));
        U32ToHexString(This->Granularity, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Can write      : "));
        StringConcat(Text, This->CanWrite ? TEXT("True") : TEXT("False"));
        StringConcat(Text, Text_NewLine);

        /*
            StringConcat(Text, "Operand Size   : ");
            StringConcat(Text, This->OperandSize ? "32-bit" : "16-bit");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Conforming     : ");
            StringConcat(Text, This->Conforming ? "True" : "False");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Present        : ");
            StringConcat(Text, This->Present ? "True" : "False");
            StringConcat(Text, Text_NewLine);
        */
    }

    return 1;
}

/***************************************************************************/

BOOL DumpGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size) {
    U32 Index = 0;

    if (Table) {
        SEGMENTINFO Info;
        STR Text[256];

        for (Index = 0; Index < Size; Index++) {
            GetSegmentInfo(Table + Index, &Info);
            SegmentInfoToString(&Info, Text);
            KernelLogText(LOG_DEBUG, Text);
        }
    }

    return TRUE;
}

/***************************************************************************/

void DumpRegisters(LPINTEL386REGISTERS Regs) {

    KernelLogText(LOG_VERBOSE, TEXT("EAX : %X EBX : %X ECX : %X EDX : %X "), Regs->EAX, Regs->EBX, Regs->ECX, Regs->EDX);
    KernelLogText(LOG_VERBOSE, TEXT("ESI : %X EDI : %X EBP : %X "), Regs->ESI, Regs->EDI, Regs->EBP);
    KernelLogText(LOG_VERBOSE, TEXT("CS : %X DS : %X SS : %X "), Regs->CS, Regs->DS, Regs->SS);
    KernelLogText(LOG_VERBOSE, TEXT("ES : %X FS : %X GS : %X "), Regs->ES, Regs->FS, Regs->GS);
    KernelLogText(LOG_VERBOSE, TEXT("E-flags : %X EIP : %X "), Regs->EFlags, Regs->EIP);
    KernelLogText(LOG_VERBOSE, TEXT("CR0 : %X CR2 : %X CR3 : %X CR4 : %X "), Regs->CR0, Regs->CR2, Regs->CR3, Regs->CR4);
    KernelLogText(LOG_VERBOSE, TEXT("DR0 : %X DR1 : %X DR2 : %X DR3 : %X "), Regs->DR0, Regs->DR1, Regs->DR2, Regs->DR3);
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
    Info->Family =
        (Regs[1].reg_EAX & INTEL_CPU_MASK_FAMILY) >> INTEL_CPU_SHFT_FAMILY;
    Info->Model =
        (Regs[1].reg_EAX & INTEL_CPU_MASK_MODEL) >> INTEL_CPU_SHFT_MODEL;
    Info->Stepping =
        (Regs[1].reg_EAX & INTEL_CPU_MASK_STEPPING) >> INTEL_CPU_SHFT_STEPPING;
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
            LockMutex(MUTEX_CONSOLE, 0);
            OldX = Console.CursorX;
            OldY = Console.CursorY;

            Console.CursorX = X;
            Console.CursorY = Y;
            ConsolePrint(Text);

            MouseX = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
            MouseY = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);
            Buttons = SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
            Console.CursorX = 0;
            Console.CursorY = 0;
            ConsolePrint(TEXT("%d %d %d"), MouseX, MouseY, Buttons);

            Console.CursorX = OldX;
            Console.CursorY = OldY;
            UnlockMutex(MUTEX_CONSOLE);
        }

        DoSystemCall(SYSCALL_Sleep, 40);
    }

    return 0;
}

/***************************************************************************/

void DumpSystemInformation() {
    KernelLogText(LOG_VERBOSE, TEXT("DumpSystemInformation"));

    //-------------------------------------
    // Print information on computer

    ConsolePrint(TEXT("Computer ID : "));
    ConsolePrint(Kernel.CPU.Name);
    ConsolePrint(Text_NewLine);

    //-------------------------------------
    // Print information on memory

    ConsolePrint(TEXT("Physical memory : %d"), Memory / 1024);
    ConsolePrint(Text_Space);
    ConsolePrint(Text_KB);
    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

void InitializeFileSystems() {
    LPLISTNODE Node;

    MountSystemFS();

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        MountDiskPartitions((LPPHYSICALDISK)Node, NULL, 0);
    }
}

/***************************************************************************/

U32 GetPhysicalMemoryUsed() {
    U32 NumPages = 0;
    U32 Index = 0;
    U32 Byte = 0;
    U32 Mask = 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    for (Index = 0; Index < Pages; Index++) {
        Byte = Index >> MUL_8;
        Mask = (U32)0x01 << (Index & 0x07);
        if (PPB[Byte] & Mask) NumPages++;
    }

    UnlockMutex(MUTEX_MEMORY);

    return (NumPages << PAGE_SIZE_MUL);
}

/***************************************************************************/

void LoadDriver(LPDRIVER Driver, LPCSTR Name) {
    if (Driver->ID != ID_DRIVER) {
        KernelLogText(LOG_ERROR, TEXT("%s driver not valid (at address %X). ID = %X. Aborting!"), Name, Driver, Driver->ID);
        SLEEPING_BEAUTY
    }
    Driver->Command(DF_LOAD, 0);
}

/***************************************************************************/

extern U32 DeadBeef;

void InitializeKernel() {
    // PROCESSINFO ProcessInfo;
    TASKINFO TaskInfo;
    KERNELSTARTUPINFO TempKernelStartup;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernel] Sarting up..."));

    //-------------------------------------
    // Dump critical information

    KernelLogText(LOG_DEBUG, TEXT("PA_SYS : %X"), PA_SYS);
    KernelLogText(LOG_DEBUG, TEXT("PA_KER : %X"), PA_KER);

    KernelLogText(LOG_DEBUG, TEXT("LA_RAM : %X"), LA_RAM);
    KernelLogText(LOG_DEBUG, TEXT("LA_VIDEO : %X"), LA_VIDEO);
    KernelLogText(LOG_DEBUG, TEXT("LA_CONSOLE : %X"), LA_CONSOLE);
    KernelLogText(LOG_DEBUG, TEXT("LA_USER : %X"), LA_USER);
    KernelLogText(LOG_DEBUG, TEXT("LA_LIBRARY : %X"), LA_LIBRARY);
    KernelLogText(LOG_DEBUG, TEXT("LA_KERNEL : %X"), LA_KERNEL);
    KernelLogText(LOG_DEBUG, TEXT("LA_RAMDISK : %X"), LA_RAMDISK);
    KernelLogText(LOG_DEBUG, TEXT("LA_SYSTEM : %X"), LA_SYSTEM);
    KernelLogText(LOG_DEBUG, TEXT("LA_DIRECTORY : %X"), LA_DIRECTORY);
    KernelLogText(LOG_DEBUG, TEXT("LA_SYSTABLE : %X"), LA_SYSTABLE);
    KernelLogText(LOG_DEBUG, TEXT("LA_PAGETABLE : %X"), LA_PAGETABLE);

    KernelLogText(LOG_DEBUG, TEXT("StubAddress : %X"), StubAddress);
    KernelLogText(LOG_DEBUG, TEXT("Stack : %X"), LA_KERNEL_STACK);

    //-------------------------------------
    // Check data integrity

    if (DeadBeef != 0xDEADBEEF) {
        KernelLogText(LOG_DEBUG, TEXT("Expected a dead beef at %X"), (&DeadBeef));
        KernelLogText(LOG_DEBUG, TEXT("Data corrupt, aborting !"));
        KernelDump((LINEAR)(&DeadBeef), 32);

        for (LINEAR Pointer = StubAddress; Pointer < N_2MB; Pointer++) {
            if (*((U32*) Pointer) == 0xDEADBEEF) {
                KernelLogText(LOG_DEBUG, TEXT("Found dead beef at %X"), Pointer);
                KernelLogText(LOG_DEBUG, TEXT("Meaning at stub + %X"), Pointer - StubAddress);
                break;
            }
        }

        SLEEPING_BEAUTY
    }

    //-------------------------------------
    // Log page tables

    LogAllPageTables(LOG_DEBUG, (const PAGEDIRECTORY*) LA_DIRECTORY);

    //-------------------------------------
    // No more interrupts

    KernelLogText(LOG_DEBUG, TEXT("Disabling interrupts"));

    DisableInterrupts();

    //-------------------------------------
    // Get system information gathered by the stub

    MemoryCopy(&TempKernelStartup, (LPVOID)(StubAddress + KERNEL_STARTUP_INFO_OFFSET), sizeof(KERNELSTARTUPINFO));

    IRQMask_21_RM = TempKernelStartup.IRQMask_21_RM;
    IRQMask_A1_RM = TempKernelStartup.IRQMask_A1_RM;

    //-------------------------------------
    // Initialize BSS after information collected

    LINEAR BSSStart = (LINEAR)(&__bss_init_start);
    LINEAR BSSEnd = (LINEAR)(&__bss_init_end);
    U32 BSSSize = BSSEnd - BSSStart;

    KernelLogText(LOG_DEBUG, TEXT("BSS start : %X, end : %X, size %X"), BSSStart, BSSEnd, BSSSize);
    MemorySet((LPVOID) BSSStart, 0, BSSSize);
    KernelLogText(LOG_DEBUG, TEXT("BSS cleared"));

    MemoryCopy(&KernelStartup, &TempKernelStartup, sizeof(KERNELSTARTUPINFO));

    KernelLogText(LOG_DEBUG, TEXT("Kernel startup info:"));
    KernelLogText(LOG_DEBUG, TEXT("  Loader_SS : %X"), KernelStartup.Loader_SS);
    KernelLogText(LOG_DEBUG, TEXT("  Loader_SP : %X"), KernelStartup.Loader_SP);
    KernelLogText(LOG_DEBUG, TEXT("  IRQMask_21_RM : %X"), KernelStartup.IRQMask_21_RM);
    KernelLogText(LOG_DEBUG, TEXT("  IRQMask_A1_RM : %X"), KernelStartup.IRQMask_A1_RM);
    KernelLogText(LOG_DEBUG, TEXT("  MemorySize : %X"), KernelStartup.MemorySize);

    //-------------------------------------
    // Initialize the VMM

    InitializeVirtualMemoryManager();
    KernelLogText(LOG_DEBUG, TEXT("Vitual memory manager initialized"));

    // Provoke page fault
    // *((U32*)0x70000000) = 5;

    //-------------------------------------
    // Initialize kernel heap

    InitializeKernelHeap();
    KernelLogText(LOG_DEBUG, TEXT("Kernel heap initialized"));

    //-------------------------------------
    // Initialize the console

    InitializeConsole();
    KernelLogText(LOG_VERBOSE, TEXT("Console initialized"));

    //-------------------------------------
    // Initialize the keyboard

    LoadDriver(&StdKeyboardDriver, TEXT("Keyboard"));
    KernelLogText(LOG_VERBOSE, TEXT("Keyboard initialized"));

    //-------------------------------------
    // Print system infomation

    DumpSystemInformation();

    //-------------------------------------
    // Initialize interrupts

    InitializeInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("Interrupts initialized"));

    //-------------------------------------
    // Setup the kernel's main task

    InitKernelTask();
    LoadInitialTaskRegister(KernelTask.Selector);
    KernelLogText(LOG_VERBOSE, TEXT("Kernel task setup"));

    //-------------------------------------
    // Initialize the clock

    InitializeClock();
    KernelLogText(LOG_VERBOSE, TEXT("Clock initialized"));

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("Interrupts enabled"));

    //-------------------------------------
    // Get information on CPU

    GetCPUInformation(&(Kernel.CPU));
    KernelLogText(LOG_VERBOSE, TEXT("Got CPU information"));

    //-------------------------------------
    // Initialize RAM drives

    LoadDriver(&RAMDiskDriver, TEXT("RAMDisk"));
    KernelLogText(LOG_VERBOSE, TEXT("RAM drive initialized"));

    //-------------------------------------
    // Initialize physical drives

    LoadDriver(&StdHardDiskDriver, TEXT("StdHardDisk"));
    KernelLogText(LOG_VERBOSE, TEXT("Physical drives initialized..."));

    //-------------------------------------
    // Initialize the file systems

    InitializeFileSystems();
    KernelLogText(LOG_VERBOSE, TEXT("File systems initialized..."));

    //-------------------------------------
    // Initialize the graphics card

    LoadDriver(&VESADriver, TEXT("VESA"));
    KernelLogText(LOG_VERBOSE, TEXT("VESA driver initialized..."));

    //-------------------------------------
    // Initialize the mouse

    LoadDriver(&SerialMouseDriver, TEXT("SerialMouse"));
    KernelLogText(LOG_VERBOSE, TEXT("Mouse initialized..."));

    //-------------------------------------
    // Print the EXOS banner

    ConsolePrint(Text_OSTitle);

    //-------------------------------------
    // Test tasks

    TaskInfo.Func      = ClockTask;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority  = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags     = 0;

    TaskInfo.Parameter = (LPVOID) (((U32) 70 << 16) | 0);
    CreateTask(&KernelProcess, &TaskInfo);

    //-------------------------------------
    // Shell task

    TaskInfo.Func = Shell;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;

    CreateTask(&KernelProcess, &TaskInfo);

    //-------------------------------------
    // Launch Portal

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
