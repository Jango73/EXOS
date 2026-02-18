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


    Interrupt routing page for the interrupt dump payload

\************************************************************************/

#include "../../kernel/include/CoreString.h"
#include "dumper-pci.h"
#include "dumper.h"

/************************************************************************/

static UINT GetIoApicRedirectionCount(void) {
    U32 VersionReg;
    UINT Count;

    EnableA20Fast();
    VersionReg = ReadIOApicRegister(IOAPIC_BASE_DEFAULT, IOAPIC_REG_VER);
    DisableA20Fast();

    Count = (UINT)(((VersionReg >> 16) & 0xFF) + 1);
    return Count;
}

/************************************************************************/

static BOOL ReadIoApicRedirection(U8 Line, UINT RedirectionCount, U32* Low, U32* High) {
    if (Line >= RedirectionCount) {
        return FALSE;
    }

    EnableA20Fast();
    *Low = ReadIOApicRegister(IOAPIC_BASE_DEFAULT, (U8)(IOAPIC_REG_REDTBL_BASE + (Line * 2)));
    *High = ReadIOApicRegister(IOAPIC_BASE_DEFAULT, (U8)(IOAPIC_REG_REDTBL_BASE + (Line * 2) + 1));
    DisableA20Fast();

    return TRUE;
}

/************************************************************************/

static void WriteControllerRouting(
    LPOUTPUT_CONTEXT Context,
    LPCSTR Name,
    U8 ClassCode,
    U8 SubClass,
    U8 ProgrammingInterface,
    UINT RedirectionCount) {
    PCI_CONTROLLER_INFO Controller;
    UINT ControllerCount = 0;
    BOOL Found = FindPciControllerByClass(
        ClassCode,
        SubClass,
        ProgrammingInterface,
        &Controller,
        &ControllerCount);
    STR Label[32];

    StringPrintFormat(Label, TEXT("%s Controllers"), Name);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("%u\r\n"), (U32)ControllerCount);

    if (!Found) {
        StringPrintFormat(Label, TEXT("%s Interrupt Route"), Name);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Not Found\r\n"));
        return;
    }

    {
        U32 RedirLow = 0;
        U32 RedirHigh = 0;
        BOOL HasRedirection = FALSE;

        if (Controller.InterruptLine != 0xFF) {
            HasRedirection = ReadIoApicRedirection(
                Controller.InterruptLine,
                RedirectionCount,
                &RedirLow,
                &RedirHigh);
        }

        StringPrintFormat(Label, TEXT("%s Interrupt Route"), Name);
        if (Controller.InterruptLine == 0xFF) {
            WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Line=Not Available Pin=%u\r\n"),
                (U32)Controller.InterruptPin);
        } else if (HasRedirection) {
            WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Line=%u Pin=%u Redirection=%x/%x\r\n"),
                (U32)Controller.InterruptLine,
                (U32)Controller.InterruptPin,
                RedirLow,
                RedirHigh);
        } else {
            WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Line=%u Pin=%u Redirection=Not Available\r\n"),
                (U32)Controller.InterruptLine,
                (U32)Controller.InterruptPin);
        }
    }
}

/************************************************************************/

/**
 * @brief Draw interrupt routing information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageInterruptRouting(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    UINT RedirectionCount = GetIoApicRedirectionCount();

    DrawPageHeader(Context, TEXT("Interrupt Routing"), PageIndex);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC Base"), TEXT("%p\r\n"), IOAPIC_BASE_DEFAULT);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC Redirections"), TEXT("%u\r\n"), (U32)RedirectionCount);

    WriteControllerRouting(Context,
        TEXT("AHCI"),
        PCI_CLASS_MASS_STORAGE,
        PCI_SUBCLASS_SATA,
        PCI_PROGIF_AHCI,
        RedirectionCount);

    WriteControllerRouting(Context,
        TEXT("EHCI"),
        PCI_CLASS_SERIAL_BUS,
        PCI_SUBCLASS_USB,
        PCI_PROGIF_EHCI,
        RedirectionCount);

    WriteControllerRouting(Context,
        TEXT("xHCI"),
        PCI_CLASS_SERIAL_BUS,
        PCI_SUBCLASS_USB,
        PCI_PROGIF_XHCI,
        RedirectionCount);

    for (UINT Line = 0; Line < RedirectionCount; Line++) {
        U32 RedirLow = 0;
        U32 RedirHigh = 0;
        STR Label[24];

        if (!ReadIoApicRedirection((U8)Line, RedirectionCount, &RedirLow, &RedirHigh)) {
            continue;
        }

        {
            U32 Vector = RedirLow & 0xFF;
            U32 Delivery = (RedirLow >> 8) & 0x7;
            U32 DestMode = (RedirLow >> 11) & 0x1;
            U32 Polarity = (RedirLow >> 13) & 0x1;
            U32 Trigger = (RedirLow >> 15) & 0x1;
            U32 Mask = (RedirLow >> 16) & 0x1;
            U32 Destination = (RedirHigh >> 24) & 0xFF;

            StringPrintFormat(Label, TEXT("Redir %u"), Line);
            WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label,
                TEXT("Vec=%x Del=%x Dst=%x Pol=%x Trg=%x Msk=%x Dest=%x\r\n"),
                Vector,
                Delivery,
                DestMode,
                Polarity,
                Trigger,
                Mask,
                Destination);
        }
    }

    DrawFooter(Context);
}
