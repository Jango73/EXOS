
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


    x86-64 Logging helpers

\************************************************************************/

#ifndef ARCH_X86_64_X86_64_LOG_H_INCLUDED
#define ARCH_X86_64_X86_64_LOG_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "process/Schedule.h"
#include "arch/x86-64/x86-64.h"

/***************************************************************************/

void LogPageDirectory64(PHYSICAL Pml4Physical);

/***************************************************************************/

void LogGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table, U32 EntryCount);

/***************************************************************************/

void LogRegisters64(const LPINTEL_64_REGISTERS Regs);
void LogTaskStateSegment(U32 LogType, const X86_64_TASK_STATE_SEGMENT* Tss);
void LogFrame(LPTASK Task, LPINTERRUPT_FRAME Frame);
void BacktraceFrom(U64 StartRbp, U32 MaxFrames);

/***************************************************************************/

#endif  // ARCH_X86_64_X86_64_LOG_H_INCLUDED
