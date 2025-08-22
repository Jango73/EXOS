
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "I386.h"

/***************************************************************************/

// Debug out on COM2
#define LOG_COM_INDEX 1

#define LOG_DEBUG 0x0001
#define LOG_VERBOSE 0x0002
#define LOG_WARNING 0x0004
#define LOG_ERROR 0x0008

/***************************************************************************/

void InitKernelLog(void);
void VarKernelPrintNumber(I32 Number, I32 Base, I32 FieldWidth, I32 Precision, I32 Flags);
void KernelPrintString(LPCSTR Text);
void KernelLogText(U32, LPCSTR, ...);

void LogRegisters(LPINTEL386REGISTERS Regs);
void LogGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size);
void LogPageDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectory);
void LogPageTable(U32 LogType, const PAGETABLE* PageTable);
void LogSegmentDescriptor(U32 LogType, const SEGMENTDESCRIPTOR* SegmentDescriptor);
void LogPageTableFromDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectoryEntry);
void LogAllPageTables(U32 LogType, const PAGEDIRECTORY* PageDirectory);
void LogTSSDescriptor(U32 LogType, const TSSDESCRIPTOR* TssDescriptor);
void LogTaskStateSegment(U32 LogType, const TASKSTATESEGMENT* Tss);

/***************************************************************************/

#endif
