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


    xHCI (USB 3.0 Host Controller) Driver

\************************************************************************/

#ifndef XHCI_H_INCLUDED
#define XHCI_H_INCLUDED

/***************************************************************************/

#include "drivers/PCI.h"

/***************************************************************************/
// Defines

#define XHCI_CLASS_SERIAL_BUS 0x0C
#define XHCI_SUBCLASS_USB 0x03
#define XHCI_PROGIF_XHCI 0x30

/***************************************************************************/
// Typedefs

typedef struct tag_XHCI_DEVICE XHCI_DEVICE, *LPXHCI_DEVICE;

/***************************************************************************/
// Typedefs

typedef BOOL (*XHCI_CONTROLLER_ENUM_CALLBACK)(LPVOID Context, U32 Bus, U32 Dev, U32 Func, U32 PortCount);
typedef BOOL (*XHCI_PORT_ENUM_CALLBACK)(LPVOID Context, U32 Bus, U32 Dev, U32 Func, U32 PortIndex, U32 PortStatus,
                                        U32 SpeedId, BOOL Connected, BOOL Enabled);

/***************************************************************************/
// External symbols

extern PCI_DRIVER XHCIDriver;
UINT XHCI_EnumerateControllers(XHCI_CONTROLLER_ENUM_CALLBACK ControllerCallback,
                               XHCI_PORT_ENUM_CALLBACK PortCallback,
                               LPVOID Context);

/***************************************************************************/

#endif  // XHCI_H_INCLUDED
