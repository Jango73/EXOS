
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


    Log I386Struct

\************************************************************************/

#include "../include/I386-MCI.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/String.h"
#include "../include/System.h"

/************************************************************************/

/**
 * @brief Logs 16 bytes of memory in hexadecimal format
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param Prefix Text prefix to display before the memory dump
 * @param Memory Pointer to the 16-byte memory buffer to log
 *
 * Displays memory contents as two groups of 8 bytes in hexadecimal format.
 * Used for debugging memory structures and data inspection.
 */
void LogMemoryLine16B(U32 LogType, LPCSTR Prefix, const U8* Memory) {
    KernelLogText(
        LogType, TEXT("%s %x %x %x %x %x %x %x %x : %x %x %x %x %x %x %x %x"), Prefix, (U32)Memory[0], (U32)Memory[1],
        (U32)Memory[2], (U32)Memory[3], (U32)Memory[4], (U32)Memory[5], (U32)Memory[6], (U32)Memory[7], (U32)Memory[8],
        (U32)Memory[9], (U32)Memory[10], (U32)Memory[11], (U32)Memory[12], (U32)Memory[13], (U32)Memory[14],
        (U32)Memory[15]);
}

/************************************************************************/

/**
 * @brief Logs a buffer of arbitrary length in hexadecimal format
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param Prefix Text prefix to display before each line
 * @param Buffer Pointer to the buffer to log
 * @param Length Size of the buffer in bytes
 *
 * Displays buffer contents as hexadecimal bytes, 16 bytes per line with spacing.
 * Handles empty buffers gracefully and formats output for readability.
 */
void LogFrameBuffer(U32 LogType, LPCSTR Prefix, const U8* Buffer, U32 Length) {
    if (Buffer == NULL || Length == 0) {
        KernelLogText(LogType, TEXT("%s <empty buffer>"), Prefix);
        return;
    }

    U8 LineBuffer[128];
    U8 TempBuffer[8];
    const U8* Pointer = Buffer;
    U32 Counter = 0;
    BOOL Space = FALSE;
    LineBuffer[0] = 0;

    while (TRUE) {

        StringPrintFormat(TempBuffer, TEXT("%02x%s"), (U32)(*Pointer++), Space ? " " : "");
        StringConcat(LineBuffer, TempBuffer);

        Counter++;
        Space = !Space;

        if (Counter > 15 | Pointer >= Buffer + Length) {
            Counter = 0;
            KernelLogText(LogType, TEXT("%s %s"), Prefix, LineBuffer);
            LineBuffer[0] = 0;
        }

        if (Pointer >= Buffer + Length) break;
    }
}

/************************************************************************/

/**
 * @brief Logs the complete state of i386 processor registers
 * @param Regs Pointer to the register structure to log
 *
 * Displays all CPU registers including general-purpose, segment, control,
 * and debug registers with their current values. Shows detailed debug
 * register flags for comprehensive processor state analysis.
 */
void LogRegisters(LPINTEL386REGISTERS Regs) {
    KernelLogText(LOG_VERBOSE, TEXT("CS : %x DS : %x SS : %x "), Regs->CS, Regs->DS, Regs->SS);
    KernelLogText(LOG_VERBOSE, TEXT("ES : %x FS : %x GS : %x "), Regs->ES, Regs->FS, Regs->GS);
    KernelLogText(
        LOG_VERBOSE, TEXT("EAX : %x EBX : %x ECX : %x EDX : %x "), Regs->EAX, Regs->EBX, Regs->ECX, Regs->EDX);
    KernelLogText(LOG_VERBOSE, TEXT("ESI : %x EDI : %x EBP : %x "), Regs->ESI, Regs->EDI, Regs->EBP);
    KernelLogText(LOG_VERBOSE, TEXT("E-flags : %x EIP : %x "), Regs->EFlags, Regs->EIP);
    KernelLogText(
        LOG_VERBOSE, TEXT("CR0 : %x CR2 : %x CR3 : %x CR4 : %x "), Regs->CR0, Regs->CR2, Regs->CR3, Regs->CR4);
    KernelLogText(
        LOG_VERBOSE, TEXT("DR0 : %x DR1 : %x DR2 : %x DR3 : %x "), Regs->DR0, Regs->DR1, Regs->DR2, Regs->DR3);
    KernelLogText(
        LOG_VERBOSE, TEXT("DR6 : B0 : %x B1 : %x B2 : %x B3 : %x BD : %x BS : %x BT : %x"), BIT_0_VALUE(Regs->DR6),
        BIT_1_VALUE(Regs->DR6), BIT_2_VALUE(Regs->DR6), BIT_3_VALUE(Regs->DR6), BIT_13_VALUE(Regs->DR6),
        BIT_14_VALUE(Regs->DR6), BIT_15_VALUE(Regs->DR6));
    KernelLogText(
        LOG_VERBOSE, TEXT("DR7 : L0 : %x G1 : %x L1 : %x G1 : %x L2 : %x G2 : %x L3 : %x G3 : %x GD : %x"),
        BIT_0_VALUE(Regs->DR7), BIT_1_VALUE(Regs->DR7), BIT_2_VALUE(Regs->DR7), BIT_3_VALUE(Regs->DR7),
        BIT_4_VALUE(Regs->DR7), BIT_5_VALUE(Regs->DR7), BIT_6_VALUE(Regs->DR7), BIT_7_VALUE(Regs->DR7),
        BIT_13_VALUE(Regs->DR7));
}

/************************************************************************/

/**
 * @brief Log register state for a task at fault.
 * @param Frame Interrupt frame with register snapshot.
 */
void LogFrame(LPTASK Task, LPINTERRUPTFRAME Frame) {
    LPPROCESS Process;

    if (Task == NULL) Task = GetCurrentTask();

    SAFE_USE(Task) {
        Process = Task->Process;

        SAFE_USE(Process) {
            KernelLogText(LOG_VERBOSE, TEXT("Task : %x (%s @ %s)"), Task, Task->Name, Process->FileName);
            KernelLogText(LOG_VERBOSE, TEXT("Registers :"));
            LogRegisters(&(Frame->Registers));
        }
    }
}

/************************************************************************/

/**
 * @brief Logs the contents of the Global Descriptor Table (GDT)
 * @param Table Pointer to the GDT array
 * @param Size Number of entries in the GDT to log
 *
 * Iterates through GDT entries, converts each descriptor to human-readable
 * format and logs the segment information. Used for debugging memory
 * segmentation and privilege levels.
 */
void LogGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size) {
    U32 Index = 0;

    if (Table) {
        SEGMENTINFO Info;
        STR Text[256];

        for (Index = 0; Index < Size; Index++) {
            GetSegmentInfo(Table + Index, &Info);
            SegmentInfoToString(&Info, Text);
            DEBUG(Text);
        }
    }
}

/************************************************************************/

/**
 * @brief Logs the detailed contents of a page directory entry
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param PageDirectory Pointer to the page directory entry to log
 *
 * Displays all fields of a page directory entry including access permissions,
 * caching attributes, and physical address. Used for debugging virtual memory
 * management and page fault analysis.
 */
void LogPageDirectoryEntry(U32 LogType, const PAGEDIRECTORY* PageDirectory) {
    KernelLogText(
        LogType,
        TEXT("PAGEDIRECTORY:\n"
             "  Present       = %u\n"
             "  ReadWrite     = %u\n"
             "  Privilege     = %u\n"
             "  WriteThrough  = %u\n"
             "  CacheDisabled = %u\n"
             "  Accessed      = %u\n"
             "  Reserved      = %u\n"
             "  PageSize      = %u\n"
             "  Global        = %u\n"
             "  User          = %u\n"
             "  Fixed         = %u\n"
             "  Address       = %X\n"),
        (U32)PageDirectory->Present, (U32)PageDirectory->ReadWrite, (U32)PageDirectory->Privilege,
        (U32)PageDirectory->WriteThrough, (U32)PageDirectory->CacheDisabled, (U32)PageDirectory->Accessed,
        (U32)PageDirectory->Reserved, (U32)PageDirectory->PageSize, (U32)PageDirectory->Global,
        (U32)PageDirectory->User, (U32)PageDirectory->Fixed, (U32)PageDirectory->Address);
}

/***************************************************************************/

/**
 * @brief Logs the detailed contents of a page table entry
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param PageTable Pointer to the page table entry to log
 *
 * Displays all fields of a page table entry including access permissions,
 * dirty bit, caching attributes, and physical address mapping. Essential
 * for debugging page-level memory management issues.
 */
void LogPageTableEntry(U32 LogType, const PAGETABLE* PageTable) {
    KernelLogText(
        LogType,
        TEXT("PAGETABLE:\n"
             "  Present       = %u\n"
             "  ReadWrite     = %u\n"
             "  Privilege     = %u\n"
             "  WriteThrough  = %u\n"
             "  CacheDisabled = %u\n"
             "  Accessed      = %u\n"
             "  Dirty         = %u\n"
             "  Reserved      = %u\n"
             "  Global        = %u\n"
             "  User          = %u\n"
             "  Fixed         = %u\n"
             "  Address       = %X\n"),
        (U32)PageTable->Present, (U32)PageTable->ReadWrite, (U32)PageTable->Privilege, (U32)PageTable->WriteThrough,
        (U32)PageTable->CacheDisabled, (U32)PageTable->Accessed, (U32)PageTable->Dirty, (U32)PageTable->Reserved,
        (U32)PageTable->Global, (U32)PageTable->User, (U32)PageTable->Fixed, (U32)PageTable->Address);
}

/***************************************************************************/

/**
 * @brief Logs the detailed contents of a segment descriptor
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param SegmentDescriptor Pointer to the segment descriptor to log
 *
 * Displays all fields of a segment descriptor including base address, limit,
 * access rights, privilege level, and granularity. Used for debugging
 * memory segmentation and privilege violations.
 */
void LogSegmentDescriptor(U32 LogType, const SEGMENTDESCRIPTOR* SegmentDescriptor) {
    KernelLogText(
        LogType,
        TEXT("SEGMENTDESCRIPTOR:\n"
             "  Limit_00_15   = %X\n"
             "  Base_00_15    = %X\n"
             "  Base_16_23    = %X\n"
             "  Accessed      = %u\n"
             "  CanWrite      = %u\n"
             "  ConformExpand = %u\n"
             "  Type          = %u\n"
             "  Segment       = %u\n"
             "  Privilege     = %u\n"
             "  Present       = %u\n"
             "  Limit_16_19   = %X\n"
             "  Available     = %u\n"
             "  Unused        = %u\n"
             "  OperandSize   = %u\n"
             "  Granularity   = %u\n"
             "  Base_24_31    = %X\n"),
        (U32)SegmentDescriptor->Limit_00_15, (U32)SegmentDescriptor->Base_00_15, (U8)SegmentDescriptor->Base_16_23,
        (U32)SegmentDescriptor->Accessed, (U32)SegmentDescriptor->CanWrite, (U32)SegmentDescriptor->ConformExpand,
        (U32)SegmentDescriptor->Type, (U32)SegmentDescriptor->Segment, (U32)SegmentDescriptor->Privilege,
        (U32)SegmentDescriptor->Present, (U8)SegmentDescriptor->Limit_16_19, (U32)SegmentDescriptor->Available,
        (U32)SegmentDescriptor->Unused, (U32)SegmentDescriptor->OperandSize, (U32)SegmentDescriptor->Granularity,
        (U8)SegmentDescriptor->Base_24_31);
}

/***************************************************************************/

/**
 * @brief Logs page table entries referenced by a page directory entry
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param PageDirectoryEntry Pointer to the page directory entry
 *
 * Maps the page table from physical to virtual memory and logs the first
 * 8 present entries. Used for debugging virtual memory layout and
 * page table structure analysis.
 */
void LogPageTableFromDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectoryEntry) {
    if (!PageDirectoryEntry->Present) {
        KernelLogText(LogType, TEXT("Page table not present (Present=0), nothing to dump.\n"));
        return;
    }

    PHYSICAL PageTablePhysicalAddress = PageDirectoryEntry->Address << PAGE_SIZE_MUL;
    LINEAR PageTableVirtualAddress = MapTempPhysicalPage(PageTablePhysicalAddress);

    KernelLogText(LogType, TEXT("\n8 first entries :"));

    const PAGETABLE* PageTable = (const PAGETABLE*)PageTableVirtualAddress;
    for (U32 PageTableIndex = 0; PageTableIndex < 8; ++PageTableIndex) {
        if (PageTable[PageTableIndex].Present) {
            LogPageTableEntry(LogType, &PageTable[PageTableIndex]);
        }
    }
}

/***************************************************************************/

/**
 * @brief Logs all present page tables in a page directory
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param PageDirectory Pointer to the page directory (1024 entries)
 *
 * Iterates through all 1024 page directory entries and logs details
 * of present page tables. Provides comprehensive view of virtual
 * memory mapping for debugging memory management issues.
 */
void LogAllPageTables(U32 LogType, const PAGEDIRECTORY* PageDirectory) {
    KernelLogText(LogType, TEXT("Page Directory at %X"), PageDirectory);
    for (U32 PageDirectoryIndex = 0; PageDirectoryIndex < 1024; ++PageDirectoryIndex) {
        if (PageDirectory[PageDirectoryIndex].Present) {
            KernelLogText(LogType, TEXT("\n==== Table [%u] ====\n"), PageDirectoryIndex);
            LogPageTableFromDirectory(LogType, &PageDirectory[PageDirectoryIndex]);
        }
    }
}

/***************************************************************************/

/**
 * @brief Logs the contents of a Task State Segment descriptor
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param TssDescriptor Pointer to the TSS descriptor to log
 *
 * Displays both raw TSS descriptor fields and computed values including
 * base address, effective limit, and size. Shows decoded view for easier
 * debugging of task switching and privilege level changes.
 */
void LogTSSDescriptor(U32 LogType, const TSSDESCRIPTOR* TssDescriptor) {
    /* Compute base, raw/effective limit */
    const U32 Base = ((U32)TssDescriptor->Base_00_15) | (((U32)TssDescriptor->Base_16_23) << 16) |
                     (((U32)TssDescriptor->Base_24_31) << 24);

    const U32 RawLimit = ((U32)TssDescriptor->Limit_00_15) | (((U32)TssDescriptor->Limit_16_19 & 0x0F) << 16);

    const U32 EffectiveLimit = (TssDescriptor->Granularity ? ((RawLimit << 12) | 0xFFF) : RawLimit);
    const U32 SizeBytes = EffectiveLimit + 1;

    /* Raw fields */
    KernelLogText(
        LogType,
        TEXT("TSSDESCRIPTOR:\n"
             "  Limit_00_15   = %X\n"
             "  Base_00_15    = %X\n"
             "  Base_16_23    = %X\n"
             "  Type          = %u\n"
             "  Privilege     = %u\n"
             "  Present       = %u\n"
             "  Limit_16_19   = %X\n"
             "  Available     = %u\n"
             "  Granularity   = %u\n"
             "  Base_24_31    = %X"),
        (U32)TssDescriptor->Limit_00_15, (U32)TssDescriptor->Base_00_15, (U32)TssDescriptor->Base_16_23,
        (U32)TssDescriptor->Type, (U32)TssDescriptor->Privilege, (U32)TssDescriptor->Present,
        (U32)TssDescriptor->Limit_16_19, (U32)TssDescriptor->Available, (U32)TssDescriptor->Granularity,
        (U32)TssDescriptor->Base_24_31);

    /* Decoded view */
    KernelLogText(
        LogType,
        TEXT("TSSDESCRIPTOR (decoded):\n"
             "  Base          = %X\n"
             "  RawLimit      = %X\n"
             "  EffLimit      = %X (%u bytes)"),
        (U32)Base, (U32)RawLimit, (U32)EffectiveLimit, (U32)SizeBytes);
}

/***************************************************************************/

/**
 * @brief Logs the complete contents of a Task State Segment
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param Tss Pointer to the TSS structure to log
 *
 * Displays all TSS fields including stack pointers for different privilege
 * levels, register values, segment selectors, and I/O permission bitmap.
 * Essential for debugging task switching and privilege transitions.
 */
void LogTaskStateSegment(U32 LogType, const TASKSTATESEGMENT* Tss) {
    KernelLogText(
        LogType,
        TEXT("TASKSTATESEGMENT @ %p (sizeof=%u):\n"
             "  BackLink  = %X\n"
             "  ESP0/SS0  = %X / %X\n"
             "  ESP1/SS1  = %X / %X\n"
             "  ESP2/SS2  = %X / %X\n"
             "  CR3       = %X\n"
             "  EIP       = %X\n"
             "  EFlags    = %X\n"
             "  EAX/ECX   = %X / %X\n"
             "  EDX/EBX   = %X / %X\n"
             "  ESP/EBP   = %X / %X\n"
             "  ESI/EDI   = %X / %X\n"
             "  ES/CS     = %X / %X\n"
             "  SS/DS     = %X / %X\n"
             "  FS/GS     = %X / %X\n"
             "  LDT       = %X\n"
             "  Trap      = %u\n"
             "  IOMap     = %X (linear @ %p)"),
        (void*)Tss, (U32)sizeof(TASKSTATESEGMENT), (U32)Tss->BackLink, (U32)Tss->ESP0, (U32)Tss->SS0, (U32)Tss->ESP1,
        (U32)Tss->SS1, (U32)Tss->ESP2, (U32)Tss->SS2, (U32)Tss->CR3, (U32)Tss->EIP, (U32)Tss->EFlags, (U32)Tss->EAX,
        (U32)Tss->ECX, (U32)Tss->EDX, (U32)Tss->EBX, (U32)Tss->ESP, (U32)Tss->EBP, (U32)Tss->ESI, (U32)Tss->EDI,
        (U32)Tss->ES, (U32)Tss->CS, (U32)Tss->SS, (U32)Tss->DS, (U32)Tss->FS, (U32)Tss->GS, (U32)Tss->LDT,
        (U32)((Tss->Trap & 1) ? 1u : 0u), (U32)Tss->IOMap, (const void*)((const U8*)Tss + (U32)Tss->IOMap));

    /* Optional – dump first 16 bytes of I/O bitmap for quick sanity */
    /*
    {
        U32 Index = 0;
        STR Line[128];
        KernelLogText(LogType, TEXT("  IOMap[0..15]:"));
        for (Index = 0; Index < 16; ++Index) {
            KernelLogText(LogType, TEXT(" %02X"), (U32)Tss->IOMapBits[Index]);
        }
        KernelLogText(LogType, TEXT("\n"));
    }
    */
}

/************************************************************************/

/**
 * @brief Logs the complete contents of a task structure
 * @param LogType Type of log message (LOG_VERBOSE, LOG_DEBUG, etc.)
 * @param Task Pointer to the task structure to log
 *
 * Displays all task fields including name, process association, status,
 * priority, function pointer, stack information, and timing data.
 * Used for debugging task scheduling and memory allocation issues.
 */
void LogTask(U32 LogType, const LPTASK Task) {
    KernelLogText(
        LogType,
        TEXT("TASK @ %x:\n"
             "  Name : %s\n"
             "  Process : %x (%s)\n"
             "  Type : %x\n"
             "  Status : %x\n"
             "  Priority : %x\n"
             "  Function : %x\n"
             "  Parameter : %x\n"
             "  ExitCode : %x\n"
             "  StackBase : %x\n"
             "  StackSize : %x\n"
             "  SysStackBase : %x\n"
             "  SysStackSize : %x\n"
             "  WakeUpTime : %x"),
        (LINEAR)Task, Task->Name, (U32)Task->Process, (Task->Process == &KernelProcess ? "K" : "U"), (U32)Task->Type,
        (U32)Task->Status, (U32)Task->Priority, (U32)Task->Function, (U32)Task->Parameter, (U32)Task->ExitCode,
        (U32)Task->StackBase, (U32)Task->StackSize, (U32)Task->SysStackBase, (U32)Task->SysStackSize,
        (U32)Task->WakeUpTime);
}

/************************************************************************/

/**
 * @brief Disassemble a few instructions at EIP for fault diagnosis.
 * @param Buffer Output buffer.
 * @param EIP Current instruction pointer.
 * @param NumInstructions Number of instructions to disassemble (default 5).
 */
void Disassemble(LPSTR Buffer, U32 EIP, U32 NumInstructions) {
    STR LineBuffer[128];
    STR DisasmBuffer[64];
    STR HexBuffer[64];

    Buffer[0] = STR_NULL;

    U8 *BasePtr = (U8 *)VMA_USER;
    U8 *CodePtr = (U8 *)EIP;

    if (EIP >= VMA_LIBRARY) BasePtr = (U8 *)VMA_LIBRARY;
    if (EIP >= VMA_KERNEL) BasePtr = (U8 *)VMA_KERNEL;

    if (IsValidMemory(EIP) && IsValidMemory(EIP + NumInstructions - 1)) {
        if (EIP < 0xFFFFF) {
            SetIntelAttributes(I16BIT, I16BIT);
        } else {
            SetIntelAttributes(I32BIT, I32BIT);
        }

        for (U32 i = 0; i < NumInstructions; i++) {
            U32 InstrLength = Intel_MachineCodeToString((LPCSTR)BasePtr, (LPCSTR)CodePtr, DisasmBuffer);

            if (InstrLength > 0 && InstrLength <= 20) {
                StringPrintFormat(HexBuffer, TEXT("%x: "), CodePtr);

                for (U32 j = 0; j < InstrLength && j < 8; j++) {
                    STR ByteHex[24];
                    StringPrintFormat(ByteHex, TEXT("%x "), CodePtr[j]);
                    StringConcat(HexBuffer, ByteHex);
                }

                while (StringLength(HexBuffer) < 40) {
                    StringConcat(HexBuffer, TEXT(" "));
                }

                StringPrintFormat(LineBuffer, TEXT("%s %s\n"), HexBuffer, DisasmBuffer);
                StringConcat(Buffer, LineBuffer);

                CodePtr += InstrLength;
            } else {
                break;
            }
        }
    } else {
        StringPrintFormat(LineBuffer, TEXT("Can't disassemble at %x (base %x)\n"), CodePtr, BasePtr);
        StringConcat(Buffer, LineBuffer);
    }
}

/************************************************************************/

/**
 * @brief Performs a stack backtrace starting from a given EBP
 * @param StartEbp Starting frame pointer (EBP)
 * @param MaxFrames Maximum number of frames to trace
 *
 * Traces the call stack by following frame pointers and logging return addresses.
 * Performs basic validation to detect corrupted stack frames.
 */
void BacktraceFrom(U32 StartEbp, U32 MaxFrames) {
    U32 Depth = 0;
    U32 Ebp = StartEbp;

    KernelLogText(LOG_VERBOSE, TEXT("Backtrace (EBP=%x, max=%u)"), StartEbp, MaxFrames);

    while (Ebp && Depth < MaxFrames) {
        // Validate the current frame pointer
        if (IsValidMemory(Ebp) == FALSE) {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  EBP=%x  [stop: invalid/suspect frame]"), Depth, Ebp);
            break;
        }

        /* Frame layout:
           [EBP+0] = saved EBP (prev)
           [EBP+4] = return address (EIP)
           [EBP+8] = first argument (optional to print) */
        U32* Fp = (U32*)Ebp;

        // Safely fetch next and return PC.
        U32 NextEbp = Fp[0];
        U32 RetAddr = Fp[1];

        if (RetAddr == 0) {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  EBP=%x  RET=? [null]"), Depth, Ebp);
            break;
        }

        LPCSTR Sym = NULL;
        // if (&SymbolLookup) Sym = SymbolLookup(RetAddr);

        if (Sym && Sym[0]) {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  EIP=%x  (%s)  EBP=%x"), Depth, RetAddr, Sym, Ebp);
        } else {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  EIP=%x  EBP=%x"), Depth, RetAddr, Ebp);
        }

        /* Advance */
        Ebp = NextEbp;
        ++Depth;
    }

    KernelLogText(LOG_VERBOSE, TEXT("Backtrace end (frames=%u)"), Depth);
}

/************************************************************************/

/**
 * @brief Performs a stack backtrace from current position
 * @param MaxFrames Maximum number of frames to trace
 *
 * Gets current EBP and traces the call stack. Wrapper around BacktraceFrom.
 */
void BacktraceFromCurrent(U32 MaxFrames) {
    BacktraceFrom(GetEBP(), MaxFrames);
}
