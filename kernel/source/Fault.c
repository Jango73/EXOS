
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/I386.h"
#include "../include/System.h"

/************************************************************************/
// Fault logging helpers (selector-aware)

static void LogSelectorFromErrorCode(LPCSTR Prefix, U32 Err) {
    U16 sel = (U16)(Err & 0xFFFFu);
    U16 idx = SELECTOR_INDEX(sel);
    U16 ti  = SELECTOR_TI(sel);
    U16 rpl = SELECTOR_RPL(sel);

    KernelLogText(LOG_ERROR, TEXT("%s error code=%X  selector=%X  index=%u  TI=%u  RPL=%u"),
                  Prefix, (U32)Err, (U32)sel, (U32)idx, (U32)ti, (U32)rpl);
}

/************************************************************************/

static void LogDescriptorAndTSSFromSelector(LPCSTR Prefix, U16 Sel) {
    U16 ti  = SELECTOR_TI(Sel);
    U16 idx = SELECTOR_INDEX(Sel);

    if (ti != 0) {
        KernelLogText(LOG_ERROR, TEXT("%s selector points to LDT (TI=1); no dump available"), Prefix);
        return;
    }

    if (idx < GDT_NUM_BASE_DESCRIPTORS) {
        KernelLogText(LOG_ERROR, TEXT("%s selector index %u is below base descriptors"), Prefix, (U32)idx);
        return;
    }

    U32 table = idx - GDT_NUM_BASE_DESCRIPTORS;
    LogTSSDescriptor(LOG_ERROR, (const TSSDESCRIPTOR*)&Kernel_i386.TTD[table]);
    LogTaskStateSegment(LOG_ERROR, (const TASKSTATESEGMENT*)(Kernel_i386.TSS + table));
}

/************************************************************************/

static void LogTR(void) {
    SELECTOR tr = GetTaskRegister();

    KernelLogText(LOG_ERROR, TEXT("TR=%X (index=%u TI=%u RPL=%u)"),
                  (U32)tr, (U32)SELECTOR_INDEX(tr), (U32)SELECTOR_TI(tr), (U32)SELECTOR_RPL(tr));
}

/************************************************************************/

static void DumpFrame(LPINTERRUPTFRAME Frame) {
    LPPROCESS Process;
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        Process = Task->Process;

        if (Process != NULL) {
            KernelLogText(LOG_VERBOSE, TEXT("Image : %s"), Process->FileName);
            KernelLogText(LOG_VERBOSE, Text_Registers);
            LogRegisters(&(Frame->Registers));
        }
    }
}

/************************************************************************/

static void PrintFaultDetails(void) {
    INTEL386REGISTERS Regs;
    LPPROCESS Process;
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        Process = Task->Process;

        if (Process != NULL) {
            KernelLogText(LOG_VERBOSE, TEXT("Image : %s"), Process->FileName);
            KernelLogText(LOG_VERBOSE, Text_Registers);

            SaveRegisters(&Regs);
            LogRegisters(&Regs);
        }
    }

    // LINEAR Table = MapPhysicalPage(Process->PageDirectory);
    // LogPageDirectory(LOG_DEBUG, Table);
}

/************************************************************************/

static void Die(void) {
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL && Task != &KernelTask) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE, INFINITY);

        FreezeScheduler();

        KillTask(GetCurrentTask());

        UnlockMutex(MUTEX_KERNEL);
        UnlockMutex(MUTEX_MEMORY);
        UnlockMutex(MUTEX_CONSOLE);

        UnfreezeScheduler();

        EnableInterrupts();
    }

    // Wait forever
    DO_THE_SLEEPING_BEAUTY;
}

/************************************************************************/

void ValidateEIPOrDie(LINEAR Address) {
    if (IsValidMemory(Address) == FALSE) {
        Die();
    }
}

/************************************************************************/

void DefaultHandler(LPINTERRUPTFRAME Frame) {
    KernelPrintString(Text_Separator);
    KernelLogText(LOG_ERROR, TEXT("Unknown interrupt"));
    DumpFrame(Frame);
    Die();
}

/************************************************************************/

void DivideErrorHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Divide error"));
    DumpFrame(Frame);
    Die();
}

/************************************************************************/

typedef struct { unsigned short Limit; unsigned int Base; } GDTR32;
typedef struct {
    unsigned short Limit0;
    unsigned short Base0;
    unsigned char  Base1;
    unsigned char  Access;
    unsigned char  Gran;
    unsigned char  Base2;
} GdtDesc;

static inline void Sgdt(GDTR32* g){ __asm__ __volatile__("sgdt %0":"=m"(*g)); }
static inline unsigned short StrSel(void){ unsigned short s; __asm__ __volatile__("str %0":"=r"(s)); return s; }
static inline unsigned int GdtBase(const GdtDesc* d){
    return (unsigned int)d->Base0 | ((unsigned int)d->Base1 << 16) | ((unsigned int)d->Base2 << 24);
}

static inline unsigned char ReadCurrentTSSTrapByte(void){
    GDTR32 gd; Sgdt(&gd);
    const GdtDesc* gdt = (const GdtDesc*)(unsigned long)gd.Base;
    unsigned short tr  = StrSel();
    const GdtDesc de   = gdt[tr >> 3];
    unsigned int  base = GdtBase(&de);
    return *(volatile unsigned char*)(unsigned long)(base + 0x64);
}

void DebugExceptionHandler_MinProbe(void){
    GDTR32 gd; Sgdt(&gd);
    const GdtDesc* gdt = (const GdtDesc*)(unsigned long)gd.Base;
    unsigned short tr  = StrSel();
    const GdtDesc de   = gdt[tr >> 3];
    unsigned int  base = GdtBase(&de);
    unsigned char trap = *(volatile unsigned char*)(unsigned long)(base + 0x64);

    KernelPrintString("[#DB] TSS base="); VarKernelPrintNumber(base, 16, 0, 0, 0);
    KernelPrintString(" Trap@+0x64=");    VarKernelPrintNumber(trap, 16, 0, 0, 0);
    KernelPrintString(Text_NewLine);
}

static int IsSpuriousTaskSwitchDB(void){
    U32 dr6 = GetDR6();
    U32 dr7 = GetDR7();

    // Keep meaningful DR6 bits
    U32 cause = dr6 & (0xFu | (1u<<13) | (1u<<14) | (1u<<15));
    if (cause != (1u<<15)) return 0;                // not BT-only

    // Only check Lx/Gx enable bits (0..7). Other DR7 bits can be non-zero harmlessly.
    if ((dr7 & 0xFFu) != 0) return 0;               // some breakpoints enabled

    // Double-check: the actual Trap byte in the TSS pointed by TR is 0
    if (ReadCurrentTSSTrapByte() != 0) return 0;

    return 1;
}

void DebugExceptionHandler(LPINTERRUPTFRAME Frame) {
    LPTASK Task = GetCurrentTask();

    KernelLogText(LOG_ERROR, TEXT("Debug exception"));

    ConsolePrint(TEXT("Debug exception !\n"));
    ConsolePrint(TEXT("The current task (%X) triggered a debug exception "), Task ? Task : 0);
    ConsolePrint(TEXT("at EIP : %X\n"), Frame->Registers.EIP);

    if (IsSpuriousTaskSwitchDB()){
        SetDR6(0);
        KernelPrintString(TEXT("IsSpuriousTaskSwitchDB returned TRUE\n"));
        DumpFrame(Frame);
        return;
    }

    U32 dr6 = GetDR6();
    KernelPrintString(TEXT("DR6 : "));
    VarKernelPrintNumber(dr6, 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    U32 gdtr = GetGDTR();
    KernelPrintString(TEXT("GDTR : "));
    VarKernelPrintNumber(gdtr, 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    U32 ldtr = GetLDTR();
    KernelPrintString(TEXT("LDTR : "));
    VarKernelPrintNumber(ldtr, 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    KernelPrintString(TEXT("LA_GDT : "));
    VarKernelPrintNumber(LA_GDT, 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    SELECTOR tr = GetTaskRegister();

    KernelPrintString(TEXT("index : "));
    VarKernelPrintNumber((U32)SELECTOR_INDEX(tr), 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    KernelPrintString(TEXT("TI : "));
    VarKernelPrintNumber((U32)SELECTOR_TI(tr), 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    KernelPrintString(TEXT("RPL : "));
    VarKernelPrintNumber((U32)SELECTOR_RPL(tr), 16, 0, 0, 0);
    KernelPrintString(Text_Space);

    KernelPrintString(Text_NewLine);

    DebugExceptionHandler_MinProbe();

    // BS (bit14) = single-step (TF=1)
    if (dr6 & (1u << 14)) KernelPrintString("[#DB] cause: single-step (TF=1)\n");
    // BT (bit15) = task-switch trap (TSS.T=1)
    if (dr6 & (1u << 15)) KernelPrintString("[#DB] cause: task-switch trap (TSS.T=1)\n");
    // B0..B3 = hardware breakpoints
    if (dr6 & 0xF)        KernelPrintString("[#DB] cause: hardware breakpoint (DR0-DR3)\n");
    // BD (bit13) = general detect
    if (dr6 & (1u << 13)) KernelPrintString("[#DB] cause: general-detect (DR7.GD)\n");

    LogTR();
    DumpFrame(Frame);

    ConsolePrint(Text_NewLine);
    ConsolePrint(TEXT("Debug exception !\n"));
    ConsolePrint(TEXT("The current task (%X) triggered a debug exception "), Task ? Task : 0);
    ConsolePrint(TEXT("at EIP : %X\n"), Frame->Registers.EIP);
    Die();
}

/************************************************************************/

void NMIHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Non-maskable interrupt"));
    DumpFrame(Frame);
}

/***************************************************************************/

void BreakPointHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Breakpoint"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void OverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Overflow"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void BoundRangeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Bound range fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void InvalidOpcodeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid opcode"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void DeviceNotAvailHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Device not available"));
    DumpFrame(Frame);
}

/***************************************************************************/

void DoubleFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Double fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void MathOverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Math overflow"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void InvalidTSSHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid TSS"));

    LogTR();
    LogSelectorFromErrorCode("[#TS]", Frame ? Frame->ErrCode : 0);
    if (Frame && Frame->ErrCode) { LogDescriptorAndTSSFromSelector("[#TS]", (U16)(Frame->ErrCode & 0xFFFFu)); }

    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void SegmentFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Segment fault"));

    LogTR();
    LogSelectorFromErrorCode("[#NP]", Frame ? Frame->ErrCode : 0);
    if (Frame && Frame->ErrCode) { LogDescriptorAndTSSFromSelector("[#NP]", (U16)(Frame->ErrCode & 0xFFFFu)); }

    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void StackFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Stack fault"));

    LogTR();
    LogSelectorFromErrorCode("[#SS]", Frame ? Frame->ErrCode : 0);
    if (Frame && Frame->ErrCode) { LogDescriptorAndTSSFromSelector("[#SS]", (U16)(Frame->ErrCode & 0xFFFFu)); }

    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

static void LogGPError(U32 err){
    KernelPrintString("[#GP] err="); VarKernelPrintNumber(err,16,0,0,0);
    KernelPrintString(" ext="); VarKernelPrintNumber(err&1,16,0,0,0);
    KernelPrintString(" idt="); VarKernelPrintNumber((err>>1)&1,16,0,0,0);
    KernelPrintString(" ti=");  VarKernelPrintNumber((err>>2)&1,16,0,0,0);
    U32 sel = err & 0xFFFC;
    KernelPrintString(" sel="); VarKernelPrintNumber(sel,16,0,0,0);
    KernelPrintString("\n");
}

/***************************************************************************/

void GeneralProtectionHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("General protection fault"));

    ConsolePrint(TEXT("General protection fault !\n"));

    /*
    LogTR();
    LogSelectorFromErrorCode("[#GP]", Frame ? Frame->ErrCode : 0);
    if (Frame && Frame->ErrCode) { LogDescriptorAndTSSFromSelector("[#GP]", (U16)(Frame->ErrCode & 0xFFFFu)); }
    */

    LogGPError(Frame->ErrCode);

    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void PageFaultHandler(U32 ErrorCode, LINEAR Address, U32 Eip) {
    LPTASK Task = GetCurrentTask();
    INTEL386REGISTERS Regs;

    ConsolePrint(TEXT("Page fault !\n"));
    ConsolePrint(TEXT("The current task (%X) did an unauthorized access "), Task ? Task : 0);
    ConsolePrint(TEXT("at linear address : %X, error code : %X, EIP : %X\n"), Address, ErrorCode, Eip);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting"));

    KernelLogText(LOG_ERROR, TEXT("Page fault at %X (EIP %X)"), Address, Eip);

    SaveRegisters(&Regs);
    Regs.EIP = Eip;
    LogRegisters(&Regs);

    Die();
}

/***************************************************************************/

void AlignmentCheckHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Alignment check fault"));
    if (Frame) { DumpFrame(Frame); }
    PrintFaultDetails();
    Die();
}
