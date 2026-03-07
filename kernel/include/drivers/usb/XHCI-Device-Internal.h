
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


    Executable

\************************************************************************/

#ifndef XHCI_DEVICE_INTERNAL_H_INCLUDED
#define XHCI_DEVICE_INTERNAL_H_INCLUDED

#include "drivers/usb/XHCI-Internal.h"

/************************************************************************/

// External functions
void XHCI_FreeUsbTree(LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_UsbTreeHasReferences(LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_WaitForCommandCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut);
BOOL XHCI_WaitForTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut);

BOOL XHCI_InitTransferRingCore(LPCSTR Tag, PHYSICAL* PhysicalOut, LINEAR* LinearOut, U32* CycleStateOut, U32* EnqueueIndexOut);
void XHCI_BuildInputContextForAddress(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
void XHCI_BuildInputContextForEp0(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_EnableSlot(LPXHCI_DEVICE Device, U8* SlotIdOut, U32* CompletionOut);
BOOL XHCI_AddressDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL XHCI_EvaluateContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);

#endif  // XHCI_DEVICE_INTERNAL_H_INCLUDED
