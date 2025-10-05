
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


    Log

\************************************************************************/

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "arch/i386/I386.h"
#include "Task.h"

/***************************************************************************/

// Debug out on COM2
#define LOG_COM_INDEX 1

#define LOG_DEBUG 0x0001
#define LOG_VERBOSE 0x0002
#define LOG_WARNING 0x0004
#define LOG_ERROR 0x0008

/***************************************************************************/

void InitKernelLog(void);
void KernelLogText(U32, LPCSTR, ...);
void KernelLogMem(U32 Type, LINEAR Memory, U32 Size);

void LogMemoryLine16B(U32 LogType, LPCSTR Prefix, const U8* Memory);
void LogFrameBuffer(U32 LogType, LPCSTR Prefix, const U8* Buffer, U32 Length);
void LogRegisters(LPINTEL386REGISTERS Regs);
void LogFrame(LPTASK Task, LPINTERRUPTFRAME Frame);
void LogGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size);
void LogPageDirectoryEntry(U32 LogType, const PAGEDIRECTORY* PageDirectory);
void LogPageTableEntry(U32 LogType, const PAGETABLE* PageTable);
void LogSegmentDescriptor(U32 LogType, const SEGMENTDESCRIPTOR* SegmentDescriptor);
void LogPageTableFromDirectory(U32 LogType, const PAGEDIRECTORY* PageDirectoryEntry);
void LogAllPageTables(U32 LogType, const PAGEDIRECTORY* PageDirectory);
void LogTSSDescriptor(U32 LogType, const TSSDESCRIPTOR* TssDescriptor);
void LogTaskStateSegment(U32 LogType, const TASKSTATESEGMENT* Tss);
void LogTask(U32 LogType, const LPTASK Task);
void Disassemble(LPSTR Buffer, U32 EIP, U32 NumInstructions);
void BacktraceFrom(U32 StartEbp, U32 MaxFrames);
void BacktraceFromCurrent(U32 MaxFrames);

/***************************************************************************/

#endif
