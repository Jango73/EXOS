
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


    AHCI page for the interrupt dump payload

\************************************************************************/

#include "dumper-pci.h"
#include "dumper.h"

/************************************************************************/

/**
 * @brief Draw AHCI PCI information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageAhci(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    PCI_CONTROLLER_INFO Controller;
    UINT ControllerCount = 0x00;
    BOOL Found = FindPciControllerByClass(
        PCI_CLASS_MASS_STORAGE,
        PCI_SUBCLASS_SATA,
        PCI_PROGIF_AHCI,
        &Controller,
        &ControllerCount);

    DrawPageHeader(Context, TEXT("AHCI"), PageIndex);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Controllers Found"), TEXT("%u\r\n"), (U32)ControllerCount);

    if (!Found) {
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("First Controller"), TEXT("Not Found\r\n"));
        DrawFooter(Context);
        return;
    }

    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Bus/Device/Function"), TEXT("%u/%u/%u\r\n"),
        (U32)Controller.Bus,
        (U32)Controller.Device,
        (U32)Controller.Function);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Vendor Identifier"), TEXT("%x\r\n"), (U32)Controller.VendorId);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Device Identifier"), TEXT("%x\r\n"), (U32)Controller.DeviceId);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Class Code"), TEXT("%x\r\n"), (U32)Controller.ClassCode);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Subclass"), TEXT("%x\r\n"), (U32)Controller.SubClass);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Programming Interface"), TEXT("%x\r\n"),
        (U32)Controller.ProgrammingInterface);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("BAR5 Base"), TEXT("%p\r\n"), Controller.Bar5Base);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Interrupt Line"), TEXT("%u\r\n"), (U32)Controller.InterruptLine);

    DrawFooter(Context);
}
