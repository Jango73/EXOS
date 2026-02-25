
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

#include "drivers/bus/PCI.h"

/***************************************************************************/
// Defines

#define XHCI_CLASS_SERIAL_BUS 0x0C
#define XHCI_SUBCLASS_USB 0x03
#define XHCI_PROGIF_XHCI 0x30

#define XHCI_ENUM_ERROR_NONE            0u
#define XHCI_ENUM_ERROR_BUSY            1u
#define XHCI_ENUM_ERROR_RESET_TIMEOUT   2u
#define XHCI_ENUM_ERROR_INVALID_SPEED   3u
#define XHCI_ENUM_ERROR_INIT_STATE      4u
#define XHCI_ENUM_ERROR_ENABLE_SLOT     5u
#define XHCI_ENUM_ERROR_ADDRESS_DEVICE  6u
#define XHCI_ENUM_ERROR_DEVICE_DESC     7u
#define XHCI_ENUM_ERROR_CONFIG_DESC     8u
#define XHCI_ENUM_ERROR_CONFIG_PARSE    9u
#define XHCI_ENUM_ERROR_SET_CONFIG     10u
#define XHCI_ENUM_ERROR_HUB_INIT       11u

#define XHCI_ENUM_COMPLETION_TIMEOUT 0xFFFFu

/***************************************************************************/
// Typedefs

typedef struct tag_XHCI_DEVICE XHCI_DEVICE, *LPXHCI_DEVICE;

/***************************************************************************/
// External symbols

extern PCI_DRIVER XHCIDriver;
void XHCI_EnsureUsbDevices(LPXHCI_DEVICE Device);

/***************************************************************************/

#endif  // XHCI_H_INCLUDED
