#ifndef XHCI_DEVICE_INTERNAL_H_INCLUDED
#define XHCI_DEVICE_INTERNAL_H_INCLUDED

#include "drivers/XHCI-Internal.h"

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
