
/************************************************************************\

    EXOS Interrupt Dump Payload
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


    Shared declarations for the interrupt dump payload

\************************************************************************/

#ifndef DUMPER_H_INCLUDED
#define DUMPER_H_INCLUDED

#include "Base.h"

/************************************************************************/
// Macros

#define OUTPUT_VALUE_COLUMN 20
#define OUTPUT_BUFFER_SIZE 8192
#define OUTPUT_MAX_LINES 256

#define IOAPIC_BASE_DEFAULT 0xFEC00000u
#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN 0x10
#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_REDTBL_BASE 0x10

/************************************************************************/
// Type definitions

typedef struct tag_OUTPUT_CONTEXT {
    STR TemporaryString[128];
    STR Buffer[OUTPUT_BUFFER_SIZE];
    UINT BufferLength;
    UINT LineCount;
    UINT LineOffsets[OUTPUT_MAX_LINES];
} OUTPUT_CONTEXT, *LPOUTPUT_CONTEXT;

/************************************************************************/
// External functions

void WriteFormatRaw(LPOUTPUT_CONTEXT Context, LPCSTR Format, ...);
void WriteFormat(LPOUTPUT_CONTEXT Context, UINT ValueColumn, LPCSTR Label, LPCSTR ValueFormat, ...);
void DrawPageHeader(LPOUTPUT_CONTEXT Context, LPCSTR Title, U8 PageIndex);
void DrawFooter(LPOUTPUT_CONTEXT Context);
void CopyFromLinear(U32 Address, void* Destination, U32 Size);
void EnableA20Fast(void);
void DisableA20Fast(void);
U32 ReadIOApicRegister(U32 Base, U8 Register);
void DrawPageAcpiMadt(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageAhci(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageEhci(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageXhci(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageLapic(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageInterruptRouting(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageIdt(LPOUTPUT_CONTEXT Context, U8 PageIndex);
void DrawPageGdt(LPOUTPUT_CONTEXT Context, U8 PageIndex);

#endif // DUMPER_H_INCLUDED
