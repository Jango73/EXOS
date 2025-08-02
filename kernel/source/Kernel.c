
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Kernel.h"

#include "Clock.h"
#include "Console.h"
#include "Driver.h"
#include "FileSys.h"
#include "HD.h"
#include "Interrupt.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "System.h"

/***************************************************************************/

STR Text_OSTitle[] =
    "EXOS - Extensible Operating System - Version 1.00\n"
    "Copyright (c) 1999-2025 Jango73.\n"
    "All rights reserved.\n";

/***************************************************************************/

KERNELSTARTUPINFO KernelStartup = {0};
LPGATEDESCRIPTOR IDT = (LPGATEDESCRIPTOR)LA_IDT;
LPSEGMENTDESCRIPTOR GDT = (LPSEGMENTDESCRIPTOR)LA_GDT;
LPTASKTSSDESCRIPTOR TTD = (LPTASKTSSDESCRIPTOR)LA_GDT_TASK;
LPTASKSTATESEGMENT TSS = (LPTASKSTATESEGMENT)LA_TSS;
LPPAGEBITMAP PPB = (LPPAGEBITMAP)LA_PPB;

/***************************************************************************/

static LIST DesktopList = {NULL,           NULL,          NULL, 0,
                           KernelMemAlloc, KernelMemFree, NULL};

/***************************************************************************/

static LIST ProcessList = {(LPLISTNODE)&KernelProcess,
                           (LPLISTNODE)&KernelProcess,
                           (LPLISTNODE)&KernelProcess,
                           1,
                           KernelMemAlloc,
                           KernelMemFree,
                           NULL};

/***************************************************************************/

static LIST TaskList = {(LPLISTNODE)&KernelTask,
                        (LPLISTNODE)&KernelTask,
                        (LPLISTNODE)&KernelTask,
                        1,
                        KernelMemAlloc,
                        KernelMemFree,
                        NULL};

/***************************************************************************/

static LIST MutexList = {(LPLISTNODE)&KernelMutex,
                         (LPLISTNODE)&ConsoleMutex,
                         (LPLISTNODE)&KernelMutex,
                         9,
                         KernelMemAlloc,
                         KernelMemFree,
                         NULL};

/***************************************************************************/

static LIST DiskList = {NULL,           NULL,          NULL, 0,
                        KernelMemAlloc, KernelMemFree, NULL};

static LIST FileSystemList = {NULL,           NULL,          NULL, 0,
                              KernelMemAlloc, KernelMemFree, NULL};

static LIST FileList = {NULL,           NULL,          NULL, 0,
                        KernelMemAlloc, KernelMemFree, NULL};

/***************************************************************************/

KERNELDATA Kernel = {&DesktopList, &ProcessList,       &TaskList,
                     &MutexList,   &DiskList,          &FileSystemList,
                     &FileList,    {"", 0, 0, 0, 0, 0}};

/***************************************************************************/

PHYSICAL StubAddress = 0;

/***************************************************************************/

static void DebugPutChar(STR Char) {
    volatile char* vram = (char*)0xB8000;
    vram[0] = Char;
}

/***************************************************************************/

LPVOID KernelMemAlloc(U32 Size) {
    // return HeapAlloc_P(&KernelProcess, Size);
    return HeapAlloc_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, Size);
}

/***************************************************************************/

void KernelMemFree(LPVOID Pointer) {
    // HeapFree_P(&KernelProcess, Pointer);
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
            KernelPrint(Text);
        }
    }

    return TRUE;
}

/***************************************************************************/

void DumpRegisters(LPINTEL386REGISTERS Regs) {
    STR Temp[32];

    KernelPrint(TEXT("EAX : "));
    U32ToHexString(Regs->EAX, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("EBX : "));
    U32ToHexString(Regs->EBX, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("ECX : "));
    U32ToHexString(Regs->ECX, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("EDX : "));
    U32ToHexString(Regs->EDX, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("ESI : "));
    U32ToHexString(Regs->ESI, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("EDI : "));
    U32ToHexString(Regs->EDI, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    /*
    KernelPrint("ESP : ");
    U32ToHexString(Regs->ESP, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);
    */

    KernelPrint(TEXT("EBP : "));
    U32ToHexString(Regs->EBP, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("CS : "));
    U32ToHexString(Regs->CS, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("DS : "));
    U32ToHexString(Regs->DS, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("SS : "));
    U32ToHexString(Regs->SS, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("ES : "));
    U32ToHexString(Regs->ES, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("FS : "));
    U32ToHexString(Regs->FS, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("GS : "));
    U32ToHexString(Regs->GS, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("E-flags : "));
    U32ToHexString(Regs->EFlags, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("EIP : "));
    U32ToHexString(Regs->EIP, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("CR0 : "));
    U32ToHexString(Regs->CR0, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("CR2 : "));
    U32ToHexString(Regs->CR2, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("CR3 : "));
    U32ToHexString(Regs->CR3, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("CR4 : "));
    U32ToHexString(Regs->CR4, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);

    //-------------------------------------

    KernelPrint(TEXT("DR0 : "));
    U32ToHexString(Regs->DR0, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("DR1 : "));
    U32ToHexString(Regs->DR1, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("DR2 : "));
    U32ToHexString(Regs->DR2, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_Space);

    KernelPrint(TEXT("DR3 : "));
    U32ToHexString(Regs->DR3, Temp);
    KernelPrint(Temp);
    KernelPrint(Text_NewLine);
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
            KernelPrint(Text);

            MouseX = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
            MouseY = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);
            Buttons = SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
            Console.CursorX = 0;
            Console.CursorY = 0;
            KernelPrint(TEXT("%d %d %d"), MouseX, MouseY, Buttons);

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
    static STR Num[16] = {0};

    KernelPrint(Text_NewLine);

    //-------------------------------------
    // Print information on computer

    KernelPrint(TEXT("Computer ID : "));
    KernelPrint(Kernel.CPU.Name);
    KernelPrint(Text_NewLine);

    //-------------------------------------
    // Print information on memory

    KernelPrint(TEXT("Physical memory : %d"), Memory / 1024);
    KernelPrint(Text_Space);
    KernelPrint(Text_KB);
    KernelPrint(Text_NewLine);

    KernelPrint(TEXT("GDT : %X -> %X"), LA_GDT, PA_GDT);
    KernelPrint(TEXT("PGD : %X -> %X"), LA_PGD, PA_PGD);
    KernelPrint(TEXT("PGS : %X -> %X"), LA_PGS, PA_PGS);
    KernelPrint(TEXT("PGK : %X -> %X"), LA_PGK, PA_PGK);
    KernelPrint(TEXT("PGL : %X -> %X"), LA_PGL, PA_PGL);
    KernelPrint(TEXT("PGH : %X -> %X"), LA_PGH, PA_PGH);
    KernelPrint(TEXT("TSS : %X -> %X"), LA_TSS, PA_TSS);
    KernelPrint(TEXT("PPB : %X -> %X"), LA_PPB, PA_PPB);
}

/***************************************************************************/

void InitializePhysicalPageBitmap() {
    U32 NumPagesUsed = 0;
    U32 Index = 0;
    U32 Byte = 0;
    U32 Value = 0;

    NumPagesUsed = (PA_KERNEL + N_2MB) >> PAGE_SIZE_MUL;

    for (Index = 0; Index < NumPagesUsed; Index++) {
        Byte = Index >> MUL_8;
        Value = (U32)0x01 << (Index & 0x07);
        PPB[Byte] |= (U8)Value;
    }
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

void LoadDriver(LPDRIVER Driver) {
    if (Driver->ID != ID_DRIVER) {
        KernelPrint(TEXT("Kernel data corrupted. Aborting!"));
        SLEEPING_BEAUTY
    }
    Driver->Command(DF_LOAD, 0);
}

/***************************************************************************/

void InitializeKernel() {
    // PROCESSINFO ProcessInfo;
    TASKINFO TaskInfo;

    DebugPutChar('N');

    //-------------------------------------
    // No more interrupts

    DisableInterrupts();

    //-------------------------------------
    // Get system information gathered by the stub

    MemoryCopy(&KernelStartup,
               (LPVOID)(StubAddress + KERNEL_STARTUP_INFO_OFFSET),
               sizeof(KERNELSTARTUPINFO));

    IRQMask_21_RM = KernelStartup.IRQMask_21_RM;
    IRQMask_A1_RM = KernelStartup.IRQMask_A1_RM;

    //-------------------------------------
    // Initialize the VMM

    InitializeVirtualMemoryManager();

    //-------------------------------------
    // Initialize the physical page bitmap

    InitializePhysicalPageBitmap();

    //-------------------------------------
    // Initialize kernel heap

    InitializeKernelHeap();

    //-------------------------------------
    // Initialize the console

    InitializeConsole();

    //-------------------------------------
    // Print system infomation

    DumpSystemInformation();

    //-------------------------------------
    // Print the EXOS banner

    KernelPrint(Text_OSTitle);

    //-------------------------------------
    // Initialize the keyboard

    LoadDriver(&StdKeyboardDriver);
    KernelLogText(LOG_VERBOSE, TEXT("Keyboard initialized..."));

    //-------------------------------------
    // Initialize interrupts

    InitializeInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("Interrupts initialized..."));

    //-------------------------------------
    // Setup the kernel's main task

    InitKernelTask();
    LoadInitialTaskRegister(KernelTask.Selector);
    KernelLogText(LOG_VERBOSE, TEXT("Kernel task setup..."));

    //-------------------------------------
    // Initialize the clock

    InitializeClock();
    KernelLogText(LOG_VERBOSE, TEXT("Clock initialized..."));

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();
    KernelLogText(LOG_VERBOSE, TEXT("Interrupts enabled..."));

    //-------------------------------------
    // Get information on CPU

    GetCPUInformation(&(Kernel.CPU));
    KernelLogText(LOG_VERBOSE, TEXT("Got CPU information..."));

    //-------------------------------------
    // Initialize RAM drives

    LoadDriver(&RAMDiskDriver);
    KernelLogText(LOG_VERBOSE, TEXT("RAM drive initialized..."));

    //-------------------------------------
    // Initialize physical drives

    LoadDriver(&StdHardDiskDriver);
    KernelLogText(LOG_VERBOSE, TEXT("Physical drives initialized..."));

    //-------------------------------------
    // Initialize the file systems

    InitializeFileSystems();
    KernelLogText(LOG_VERBOSE, TEXT("File systems initialized..."));

    //-------------------------------------
    // Initialize the graphics card

    LoadDriver(&VESADriver);
    KernelLogText(LOG_VERBOSE, TEXT("VESA driver initialized..."));

    //-------------------------------------
    // Initialize the mouse

    LoadDriver(&SerialMouseDriver);
    KernelLogText(LOG_VERBOSE, TEXT("Mouse initialized..."));

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
    // Launch the explorer

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
