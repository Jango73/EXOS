
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Log.h"
#include "../include/Memory.h"

/************************************************************************/

void LogRegisters(LPINTEL386REGISTERS Regs) {
    KernelLogText(
        LOG_VERBOSE, TEXT("EAX : %X EBX : %X ECX : %X EDX : %X "), Regs->EAX, Regs->EBX, Regs->ECX, Regs->EDX);
    KernelLogText(LOG_VERBOSE, TEXT("ESI : %X EDI : %X EBP : %X "), Regs->ESI, Regs->EDI, Regs->EBP);
    KernelLogText(LOG_VERBOSE, TEXT("CS : %X DS : %X SS : %X "), Regs->CS, Regs->DS, Regs->SS);
    KernelLogText(LOG_VERBOSE, TEXT("ES : %X FS : %X GS : %X "), Regs->ES, Regs->FS, Regs->GS);
    KernelLogText(LOG_VERBOSE, TEXT("E-flags : %X EIP : %X "), Regs->EFlags, Regs->EIP);
    KernelLogText(
        LOG_VERBOSE, TEXT("CR0 : %X CR2 : %X CR3 : %X CR4 : %X "), Regs->CR0, Regs->CR2, Regs->CR3, Regs->CR4);
    KernelLogText(
        LOG_VERBOSE, TEXT("DR0 : %X DR1 : %X DR2 : %X DR3 : %X "), Regs->DR0, Regs->DR1, Regs->DR2, Regs->DR3);
    KernelLogText(
        LOG_VERBOSE, TEXT("DR4 : %X DR5 : %X DR6 : %X DR7 : %X "), Regs->DR4, Regs->DR5, Regs->DR6, Regs->DR7);
}

/************************************************************************/

void LogGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size) {
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
}

/************************************************************************/

void LogPageDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectory) {
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

void LogPageTable(U32 LogType, const PAGETABLE* PageTable) {
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

void LogPageTableFromDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectoryEntry) {
    if (!PageDirectoryEntry->Present) {
        KernelLogText(LogType, TEXT("Page table not present (Present=0), nothing to dump.\n"));
        return;
    }

    PHYSICAL PageTablePhysicalAddress = PageDirectoryEntry->Address << PAGE_SIZE_MUL;
    LINEAR PageTableVirtualAddress = MapPhysicalPage(PageTablePhysicalAddress);

    KernelLogText(LogType, TEXT("\n8 first entries :"));

    const PAGETABLE* PageTable = (const PAGETABLE*)PageTableVirtualAddress;
    for (U32 PageTableIndex = 0; PageTableIndex < 8; ++PageTableIndex) {
        if (PageTable[PageTableIndex].Present) {
            LogPageTable(LogType, &PageTable[PageTableIndex]);
        }
    }
}

/***************************************************************************/

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

void LogTSSDescriptor(U32 LogType, const TSSDESCRIPTOR* TssDescriptor) {
    /* Compute base, raw/effective limit */
    const U32 Base =
        ((U32)TssDescriptor->Base_00_15) |
        (((U32)TssDescriptor->Base_16_23) << 16) |
        (((U32)TssDescriptor->Base_24_31) << 24);

    const U32 RawLimit =
        ((U32)TssDescriptor->Limit_00_15) |
        (((U32)TssDescriptor->Limit_16_19 & 0x0F) << 16);

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
        (U32)TssDescriptor->Limit_00_15,
        (U32)TssDescriptor->Base_00_15,
        (U32)TssDescriptor->Base_16_23,
        (U32)TssDescriptor->Type,
        (U32)TssDescriptor->Privilege,
        (U32)TssDescriptor->Present,
        (U32)TssDescriptor->Limit_16_19,
        (U32)TssDescriptor->Available,
        (U32)TssDescriptor->Granularity,
        (U32)TssDescriptor->Base_24_31
    );

    /* Decoded view */
    KernelLogText(
        LogType,
        TEXT("TSSDESCRIPTOR (decoded):\n"
             "  Base          = %X\n"
             "  RawLimit      = %X\n"
             "  EffLimit      = %X (%u bytes)"),
        (U32)Base, (U32)RawLimit, (U32)EffectiveLimit, (U32)SizeBytes
    );
}

/***************************************************************************/

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
        (void*)Tss, (U32)sizeof(TASKSTATESEGMENT),
        (U32)Tss->BackLink,
        (U32)Tss->ESP0, (U32)Tss->SS0,
        (U32)Tss->ESP1, (U32)Tss->SS1,
        (U32)Tss->ESP2, (U32)Tss->SS2,
        (U32)Tss->CR3,
        (U32)Tss->EIP,
        (U32)Tss->EFlags,
        (U32)Tss->EAX, (U32)Tss->ECX,
        (U32)Tss->EDX, (U32)Tss->EBX,
        (U32)Tss->ESP, (U32)Tss->EBP,
        (U32)Tss->ESI, (U32)Tss->EDI,
        (U32)Tss->ES, (U32)Tss->CS,
        (U32)Tss->SS, (U32)Tss->DS,
        (U32)Tss->FS, (U32)Tss->GS,
        (U32)Tss->LDT,
        (U32)((Tss->Trap & 1) ? 1u : 0u),
        (U32)Tss->IOMap,
        (const void*)((const U8*)Tss + (U32)Tss->IOMap)
    );

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
