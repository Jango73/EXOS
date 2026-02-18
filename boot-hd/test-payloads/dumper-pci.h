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


    PCI helpers for test payloads

\************************************************************************/

#ifndef DUMPER_PCI_H_INCLUDED
#define DUMPER_PCI_H_INCLUDED

#include "Base.h"

/************************************************************************/
// Macros

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define PCI_PROGIF_AHCI 0x01

#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI 0x20
#define PCI_PROGIF_XHCI 0x30

/************************************************************************/
// Type definitions

typedef struct tag_PCI_CONTROLLER_INFO {
    U8 Bus;
    U8 Device;
    U8 Function;
    U16 VendorId;
    U16 DeviceId;
    U8 ClassCode;
    U8 SubClass;
    U8 ProgrammingInterface;
    U32 Bar0Base;
    U32 Bar5Base;
    U8 InterruptLine;
    U8 InterruptPin;
} PCI_CONTROLLER_INFO, *LPPCI_CONTROLLER_INFO;

/************************************************************************/
// External functions

BOOL FindPciControllerByClass(
    U8 ClassCode,
    U8 SubClass,
    U8 ProgrammingInterface,
    LPPCI_CONTROLLER_INFO Info,
    UINT* ControllerCount);

#endif // DUMPER_PCI_H_INCLUDED
