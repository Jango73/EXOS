
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


    Log I386Struct header

\************************************************************************/

#ifndef I386_LOG_H_INCLUDED
#define I386_LOG_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Task.h"
#include "arch/i386/i386.h"
#include "arch/i386/InterruptFrame.h"

/***************************************************************************/

void LogMemoryLine16B(U32 LogType, LPCSTR Prefix, const U8* Memory);
void LogFrameBuffer(U32 LogType, LPCSTR Prefix, const U8* Buffer, U32 Length);
void LogRegisters(LPINTEL_386_REGISTERS Regs);
void LogFrame(LPTASK Task, LPINTERRUPT_FRAME Frame);
void LogGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table, U32 Size);
void LogPageDirectoryEntry(U32 LogType, const PAGE_DIRECTORY* PageDirectory);
void LogPageDirectory(PHYSICAL DirectoryPhysical);
void LogPageTableEntry(U32 LogType, const PAGE_TABLE* PageTable);
void LogSegmentDescriptor(U32 LogType, const SEGMENT_DESCRIPTOR* SegmentDescriptor);
void LogPageTableFromDirectory(U32 LogType, const PAGE_DIRECTORY* PageDirectoryEntry);
void LogAllPageTables(U32 LogType, const PAGE_DIRECTORY* PageDirectory);
void LogTSSDescriptor(U32 LogType, const TSS_DESCRIPTOR* TssDescriptor);
void LogTaskStateSegment(U32 LogType, const TASK_STATE_SEGMENT* Tss);
void LogTask(U32 LogType, const LPTASK Task);
void BacktraceFrom(U32 StartEbp, U32 MaxFrames);
void BacktraceFromCurrent(U32 MaxFrames);

/***************************************************************************/

#endif  // I386_LOG_H_INCLUDED
