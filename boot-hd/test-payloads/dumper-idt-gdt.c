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


    IDT and GDT pages for the interrupt dump payload

\************************************************************************/

#include "../../kernel/include/CoreString.h"
#include "../include/vbr-realmode-utils.h"
#include "dumper.h"

/************************************************************************/
// Type definitions

typedef struct PACKED tag_DESCRIPTOR_TABLE_PTR {
    U16 Limit;
    U32 Base;
} DESCRIPTOR_TABLE_PTR, *LPDESCRIPTOR_TABLE_PTR;

typedef struct PACKED tag_IDT_ENTRY_32 {
    U16 OffsetLow;
    U16 Selector;
    U8 Zero;
    U8 TypeAttr;
    U16 OffsetHigh;
} IDT_ENTRY_32, *LPIDT_ENTRY_32;

typedef struct PACKED tag_GDT_ENTRY {
    U16 LimitLow;
    U16 BaseLow;
    U8 BaseMid;
    U8 Access;
    U8 Granularity;
    U8 BaseHigh;
} GDT_ENTRY, *LPGDT_ENTRY;

/************************************************************************/

/**
 * @brief Draw IDT information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageIdt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    DESCRIPTOR_TABLE_PTR Idtr;
    IDT_ENTRY_32 Entry;

    BootStoreIdt((U32)&Idtr);

    DrawPageHeader(Context, TEXT("IDT"), PageIndex);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IDT Base"), TEXT("%p\r\n"), Idtr.Base);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IDT Limit"), TEXT("%x\r\n"), Idtr.Limit);

    for (U32 Vector = 0x20; Vector < 0x24; Vector++) {
        STR Label[24];
        CopyFromLinear(Idtr.Base + Vector * sizeof(IDT_ENTRY_32), &Entry, sizeof(Entry));
        U32 Offset = ((U32)Entry.OffsetHigh << 16) | Entry.OffsetLow;
        StringPrintFormat(Label, TEXT("Vec %x"), Vector);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Off=%x Sel=%x\r\n"),
            Offset, (U32)Entry.Selector);
    }

    DrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw GDT information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageGdt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    DESCRIPTOR_TABLE_PTR Gdtr;
    GDT_ENTRY Entry;

    BootStoreGdt((U32)&Gdtr);

    DrawPageHeader(Context, TEXT("GDT"), PageIndex);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("GDT Base"), TEXT("%p\r\n"), Gdtr.Base);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("GDT Limit"), TEXT("%x\r\n"), Gdtr.Limit);

    for (U32 Index = 0; Index < 4; Index++) {
        STR Label[24];
        CopyFromLinear(Gdtr.Base + Index * sizeof(GDT_ENTRY), &Entry, sizeof(Entry));
        U32 Base = (U32)Entry.BaseLow |
            ((U32)Entry.BaseMid << 16) |
            ((U32)Entry.BaseHigh << 24);
        U32 Limit = (U32)Entry.LimitLow | (((U32)Entry.Granularity & 0x0F) << 16);
        StringPrintFormat(Label, TEXT("Idx %u"), Index);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Base=%p Lim=%x\r\n"),
            Base, Limit);
    }

    DrawFooter(Context);
}
