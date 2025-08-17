
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

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

/***************************************************************************/

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
             "  Address       = 0x%05lX\n"),
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
             "  Address       = 0x%05lX\n"),
        (U32)PageTable->Present, (U32)PageTable->ReadWrite, (U32)PageTable->Privilege, (U32)PageTable->WriteThrough,
        (U32)PageTable->CacheDisabled, (U32)PageTable->Accessed, (U32)PageTable->Dirty, (U32)PageTable->Reserved,
        (U32)PageTable->Global, (U32)PageTable->User, (U32)PageTable->Fixed, (U32)PageTable->Address);
}

/***************************************************************************/

void LogSegmentDescriptor(U32 LogType, const SEGMENTDESCRIPTOR* SegmentDescriptor) {
    KernelLogText(
        LogType,
        TEXT("SEGMENTDESCRIPTOR:\n"
             "  Limit_00_15   = 0x%04X\n"
             "  Base_00_15    = 0x%04X\n"
             "  Base_16_23    = 0x%02X\n"
             "  Accessed      = %u\n"
             "  CanWrite      = %u\n"
             "  ConformExpand = %u\n"
             "  Type          = %u\n"
             "  Segment       = %u\n"
             "  Privilege     = %u\n"
             "  Present       = %u\n"
             "  Limit_16_19   = 0x%01X\n"
             "  Available     = %u\n"
             "  Unused        = %u\n"
             "  OperandSize   = %u\n"
             "  Granularity   = %u\n"
             "  Base_24_31    = 0x%02X\n"),
        (U16)SegmentDescriptor->Limit_00_15, (U16)SegmentDescriptor->Base_00_15, (U8)SegmentDescriptor->Base_16_23,
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
