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


    Local APIC page for the interrupt dump payload

\************************************************************************/

#include "dumper.h"

/************************************************************************/
// Macros

#define LAPIC_BASE_DEFAULT 0xFEE00000u

#define LAPIC_REG_ID 0x20
#define LAPIC_REG_VERSION 0x30
#define LAPIC_REG_TPR 0x80
#define LAPIC_REG_SVR 0xF0
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_ERROR 0x370

/************************************************************************/

static U32 ReadLinearU32Value(U32 Address) {
    U32 Value = 0;

    CopyFromLinear(Address, &Value, sizeof(Value));

    return Value;
}

/************************************************************************/

/**
 * @brief Draw Local APIC information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageLapic(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    U32 LapicBase = LAPIC_BASE_DEFAULT;
    U32 IdReg;
    U32 VersionReg;
    U32 TprReg;
    U32 SvrReg;
    U32 LvtTimerReg;
    U32 LvtLint0Reg;
    U32 LvtLint1Reg;
    U32 LvtErrorReg;

    EnableA20Fast();
    IdReg = ReadLinearU32Value(LapicBase + LAPIC_REG_ID);
    VersionReg = ReadLinearU32Value(LapicBase + LAPIC_REG_VERSION);
    TprReg = ReadLinearU32Value(LapicBase + LAPIC_REG_TPR);
    SvrReg = ReadLinearU32Value(LapicBase + LAPIC_REG_SVR);
    LvtTimerReg = ReadLinearU32Value(LapicBase + LAPIC_REG_LVT_TIMER);
    LvtLint0Reg = ReadLinearU32Value(LapicBase + LAPIC_REG_LVT_LINT0);
    LvtLint1Reg = ReadLinearU32Value(LapicBase + LAPIC_REG_LVT_LINT1);
    LvtErrorReg = ReadLinearU32Value(LapicBase + LAPIC_REG_LVT_ERROR);
    DisableA20Fast();

    DrawPageHeader(Context, TEXT("Local APIC"), PageIndex);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Local APIC Base"), TEXT("%p\r\n"), LapicBase);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("APIC Identifier"), TEXT("%x\r\n"), (U32)((IdReg >> 24) & 0xFF));
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("APIC Version"), TEXT("%x\r\n"), (U32)(VersionReg & 0xFF));
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Maximum LVT Entry"), TEXT("%u\r\n"),
        (U32)((VersionReg >> 16) & 0xFF));
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Task Priority"), TEXT("%x\r\n"), TprReg);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Spurious Vector"), TEXT("%x\r\n"), SvrReg);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("LVT Timer"), TEXT("%x\r\n"), LvtTimerReg);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("LVT LINT0"), TEXT("%x\r\n"), LvtLint0Reg);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("LVT LINT1"), TEXT("%x\r\n"), LvtLint1Reg);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("LVT Error"), TEXT("%x\r\n"), LvtErrorReg);

    DrawFooter(Context);
}
