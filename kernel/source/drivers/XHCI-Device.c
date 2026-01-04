
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


    xHCI

\\************************************************************************/

#include "drivers/XHCI-Internal.h"

/************************************************************************/

/**
 * @brief Initialize USB device object fields for xHCI.
 *
 * LISTNODE_FIELDS are expected to be initialized by CreateKernelObject.
 * @param Device xHCI controller.
 * @param UsbDevice USB device state.
 */
void XHCI_InitUsbDeviceObject(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    MemorySet(&UsbDevice->Mutex, 0, sizeof(XHCI_USB_DEVICE) - sizeof(LISTNODE));
    UsbDevice->Controller = Device;

    InitMutex(&UsbDevice->Mutex);
    UsbDevice->Contexts.First = NULL;
    UsbDevice->Contexts.Last = NULL;
    UsbDevice->Contexts.Current = NULL;
    UsbDevice->Contexts.NumItems = 0;
    UsbDevice->Contexts.MemAllocFunc = KernelHeapAlloc;
    UsbDevice->Contexts.MemFreeFunc = KernelHeapFree;
    UsbDevice->Contexts.Destructor = NULL;
}

/************************************************************************/

/**
 * @brief Free USB configuration tree.
 * @param UsbDevice USB device state.
 */
static void XHCI_FreeUsbTree(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList != NULL) {
        for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
            LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Endpoint->Parent;
            if (Interface == NULL || Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }
            if (Endpoint->References <= 1U) {
                if (Endpoint->TransferRingLinear) {
                    FreeRegion(Endpoint->TransferRingLinear, PAGE_SIZE);
                    Endpoint->TransferRingLinear = 0;
                }
                if (Endpoint->TransferRingPhysical) {
                    FreePhysicalPage(Endpoint->TransferRingPhysical);
                    Endpoint->TransferRingPhysical = 0;
                }
            }
            ReleaseKernelObject(Endpoint);
        }
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList != NULL) {
        for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
            if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }
            ReleaseKernelObject(Interface);
        }
    }

    if (UsbDevice->Configs != NULL) {
        KernelHeapFree(UsbDevice->Configs);
        UsbDevice->Configs = NULL;
    }

    UsbDevice->ConfigCount = 0;
    UsbDevice->SelectedConfigValue = 0;
}

/************************************************************************/

/**
 * @brief Check if any USB interface or endpoint is still referenced.
 * @param UsbDevice USB device state.
 * @return TRUE when references are still held.
 */
static BOOL XHCI_UsbTreeHasReferences(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->References > 1U) {
            return TRUE;
        }
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Endpoint->Parent;
        if (Interface == NULL || Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Endpoint->References > 1U) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Free per-device allocations excluding child nodes.
 * @param UsbDevice USB device state.
 */
static void XHCI_FreeUsbDeviceResources(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    if (UsbDevice->References > 1U) {
        UsbDevice->DestroyPending = TRUE;
        return;
    }

    if (XHCI_UsbTreeHasReferences(UsbDevice)) {
        UsbDevice->DestroyPending = TRUE;
        return;
    }

    XHCI_FreeUsbTree(UsbDevice);

    if (UsbDevice->TransferRingLinear) {
        FreeRegion(UsbDevice->TransferRingLinear, PAGE_SIZE);
        UsbDevice->TransferRingLinear = 0;
    }
    if (UsbDevice->TransferRingPhysical) {
        FreePhysicalPage(UsbDevice->TransferRingPhysical);
        UsbDevice->TransferRingPhysical = 0;
    }
    if (UsbDevice->InputContextLinear) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        UsbDevice->InputContextLinear = 0;
    }
    if (UsbDevice->InputContextPhysical) {
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextPhysical = 0;
    }
    if (UsbDevice->DeviceContextLinear) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        UsbDevice->DeviceContextLinear = 0;
    }
    if (UsbDevice->DeviceContextPhysical) {
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        UsbDevice->DeviceContextPhysical = 0;
    }
    if (UsbDevice->HubStatusLinear) {
        FreeRegion(UsbDevice->HubStatusLinear, PAGE_SIZE);
        UsbDevice->HubStatusLinear = 0;
    }
    if (UsbDevice->HubStatusPhysical) {
        FreePhysicalPage(UsbDevice->HubStatusPhysical);
        UsbDevice->HubStatusPhysical = 0;
    }
    if (UsbDevice->HubChildren != NULL) {
        KernelHeapFree(UsbDevice->HubChildren);
        UsbDevice->HubChildren = NULL;
    }
    if (UsbDevice->HubPortStatus != NULL) {
        KernelHeapFree(UsbDevice->HubPortStatus);
    UsbDevice->HubPortStatus = NULL;
    }

    UsbDevice->Present = FALSE;
    UsbDevice->DestroyPending = FALSE;
    UsbDevice->SlotId = 0;
    UsbDevice->Address = 0;
    UsbDevice->IsHub = FALSE;
    UsbDevice->HubPortCount = 0;
    UsbDevice->HubInterruptEndpoint = NULL;
    UsbDevice->HubInterruptLength = 0;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);
    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->Parent = NULL;
    UsbDevice->ParentPort = 0;
    UsbDevice->Depth = 0;
    UsbDevice->RouteString = 0;
    UsbDevice->Controller = NULL;
}

/************************************************************************/

/**
 * @brief Increment references on a USB device object.
 * @param UsbDevice USB device state.
 */
void XHCI_ReferenceUsbDevice(LPXHCI_USB_DEVICE UsbDevice) {
    SAFE_USE_VALID_ID(UsbDevice, KOID_USBDEVICE) {
        if (UsbDevice->References < MAX_UINT) {
            UsbDevice->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB device object.
 * @param UsbDevice USB device state.
 */
void XHCI_ReleaseUsbDevice(LPXHCI_USB_DEVICE UsbDevice) {
    SAFE_USE_VALID_ID(UsbDevice, KOID_USBDEVICE) {
        if (UsbDevice->References != 0) {
            ReleaseKernelObject(UsbDevice);
        }

        if (!UsbDevice->DestroyPending || XHCI_UsbTreeHasReferences(UsbDevice)) {
            return;
        }

        if ((UsbDevice->IsRootPort && UsbDevice->References == 1) ||
            (!UsbDevice->IsRootPort && UsbDevice->References == 0)) {
            XHCI_FreeUsbDeviceResources(UsbDevice);
        }
    }
}

/************************************************************************/

/**
 * @brief Increment references on a USB interface.
 * @param Interface USB interface.
 */
void XHCI_ReferenceUsbInterface(LPXHCI_USB_INTERFACE Interface) {
    SAFE_USE_VALID_ID(Interface, KOID_USBINTERFACE) {
        if (Interface->References < MAX_UINT) {
            Interface->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB interface.
 * @param Interface USB interface.
 */
void XHCI_ReleaseUsbInterface(LPXHCI_USB_INTERFACE Interface) {
    SAFE_USE_VALID_ID(Interface, KOID_USBINTERFACE) {
        if (Interface->References != 0) {
            ReleaseKernelObject(Interface);
        }
    }
}

/************************************************************************/

/**
 * @brief Increment references on a USB endpoint.
 * @param Endpoint USB endpoint.
 */
void XHCI_ReferenceUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint) {
    SAFE_USE_VALID_ID(Endpoint, KOID_USBENDPOINT) {
        if (Endpoint->References < MAX_UINT) {
            Endpoint->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB endpoint.
 * @param Endpoint USB endpoint.
 */
void XHCI_ReleaseUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint) {
    SAFE_USE_VALID_ID(Endpoint, KOID_USBENDPOINT) {
        if (Endpoint->References != 0) {
            ReleaseKernelObject(Endpoint);
        }

        if (Endpoint->References == 0) {
            if (Endpoint->TransferRingLinear) {
                FreeRegion(Endpoint->TransferRingLinear, PAGE_SIZE);
                Endpoint->TransferRingLinear = 0;
            }
            if (Endpoint->TransferRingPhysical) {
                FreePhysicalPage(Endpoint->TransferRingPhysical);
                Endpoint->TransferRingPhysical = 0;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Reset a transfer ring to an empty state.
 * @param RingPhysical Ring physical base.
 * @param RingLinear Ring linear base.
 * @param CycleStateOut Cycle state pointer.
 * @param EnqueueIndexOut Enqueue index pointer.
 */
static void XHCI_ResetTransferRingState(PHYSICAL RingPhysical, LINEAR RingLinear,
                                        U32* CycleStateOut, U32* EnqueueIndexOut) {
    if (RingPhysical == 0 || RingLinear == 0 || CycleStateOut == NULL || EnqueueIndexOut == NULL) {
        return;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)RingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(RingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    *CycleStateOut = 1;
    *EnqueueIndexOut = 0;
}

/************************************************************************/

/**
 * @brief Wait for a command completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Command TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
static BOOL XHCI_WaitForCommandCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut) {
    U32 Timeout = XHCI_EVENT_TIMEOUT_MS;

    LockMutex(&(Device->Mutex), INFINITY);
    if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT, TrbPhysical, SlotIdOut, CompletionOut)) {
        UnlockMutex(&(Device->Mutex));
        return TRUE;
    }

    while (Timeout > 0) {
        XHCI_PollCompletions(Device);
        if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT, TrbPhysical, SlotIdOut, CompletionOut)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        Sleep(1);
        Timeout--;
    }

    UnlockMutex(&(Device->Mutex));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Wait for a transfer completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Status TRB physical address.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
static BOOL XHCI_WaitForTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut) {
    U32 Timeout = XHCI_EVENT_TIMEOUT_MS;

    LockMutex(&(Device->Mutex), INFINITY);
    if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut)) {
        UnlockMutex(&(Device->Mutex));
        return TRUE;
    }

    while (Timeout > 0) {
        XHCI_PollCompletions(Device);
        if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        Sleep(1);
        Timeout--;
    }

    UnlockMutex(&(Device->Mutex));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Issue a STOP_ENDPOINT command for an endpoint.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Dci Endpoint DCI.
 * @return TRUE on success.
 */
static BOOL XHCI_StopEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0 || Dci == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_STOP_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)Dci << 16) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        WARNING(TEXT("[XHCI_StopEndpoint] Slot=%x DCI=%x completion %x"),
                (U32)UsbDevice->SlotId,
                (U32)Dci,
                Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Issue a RESET_ENDPOINT command for an endpoint.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Dci Endpoint DCI.
 * @return TRUE on success.
 */
static BOOL XHCI_ResetEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0 || Dci == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_RESET_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)Dci << 16) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        WARNING(TEXT("[XHCI_ResetEndpoint] Slot=%x DCI=%x completion %x"),
                (U32)UsbDevice->SlotId,
                (U32)Dci,
                Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Issue a DISABLE_SLOT command for a USB device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_DisableSlot(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_DISABLE_SLOT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        WARNING(TEXT("[XHCI_DisableSlot] Slot=%x completion %x"),
                (U32)UsbDevice->SlotId,
                Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Stop endpoints and reset transfer rings for a device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_TeardownDeviceTransfers(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return;
    }

    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);

    if (UsbDevice->SlotId == 0) {
        return;
    }

    if (UsbDevice->TransferRingPhysical != 0 && UsbDevice->TransferRingLinear != 0) {
        (void)XHCI_StopEndpoint(Device, UsbDevice, XHCI_EP0_DCI);
        (void)XHCI_ResetEndpoint(Device, UsbDevice, XHCI_EP0_DCI);
        XHCI_ResetTransferRingState(UsbDevice->TransferRingPhysical,
                                    UsbDevice->TransferRingLinear,
                                    &UsbDevice->TransferRingCycleState,
                                    &UsbDevice->TransferRingEnqueueIndex);
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    LPLIST EndpointList = GetUsbEndpointList();
    if (InterfaceList != NULL && EndpointList != NULL) {
        for (LPLISTNODE IfNode = InterfaceList->First; IfNode != NULL; IfNode = IfNode->Next) {
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)IfNode;
            if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }

            for (LPLISTNODE EpNode = EndpointList->First; EpNode != NULL; EpNode = EpNode->Next) {
                LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)EpNode;
                if (Endpoint->Parent != (LPLISTNODE)Interface) {
                    continue;
                }
                if (Endpoint->Dci == 0) {
                    continue;
                }

                (void)XHCI_StopEndpoint(Device, UsbDevice, Endpoint->Dci);
                (void)XHCI_ResetEndpoint(Device, UsbDevice, Endpoint->Dci);
                XHCI_ResetTransferRingState(Endpoint->TransferRingPhysical,
                                            Endpoint->TransferRingLinear,
                                            &Endpoint->TransferRingCycleState,
                                            &Endpoint->TransferRingEnqueueIndex);
            }
        }
    }

    if (XHCI_DisableSlot(Device, UsbDevice)) {
        if (Device->DcbaaLinear != 0) {
            ((U64*)Device->DcbaaLinear)[UsbDevice->SlotId] = U64_FromUINT(0);
        }
    }
}

/************************************************************************/

/**
 * @brief Add a device to the controller list.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
void XHCI_AddDeviceToList(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return;
    }

    LPLIST UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        if (Node == (LPLISTNODE)UsbDevice) {
            return;
        }
    }

    UsbDevice->Controller = Device;
    (void)ListAddItemWithParent(UsbDeviceList, UsbDevice, UsbDevice->Parent);
}

/************************************************************************/

/**
 * @brief Destroy a USB device and its children.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param FreeSelf TRUE when the UsbDevice object should be released.
 */
void XHCI_DestroyUsbDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, BOOL FreeSelf) {
    if (UsbDevice == NULL) {
        return;
    }

    UsbDevice->Present = FALSE;
    UsbDevice->DestroyPending = TRUE;

    if (UsbDevice->IsHub && UsbDevice->HubChildren != NULL) {
        for (U32 PortIndex = 0; PortIndex < UsbDevice->HubPortCount; PortIndex++) {
            LPXHCI_USB_DEVICE Child = UsbDevice->HubChildren[PortIndex];
            if (Child != NULL) {
                UsbDevice->HubChildren[PortIndex] = NULL;
                XHCI_DestroyUsbDevice(Device, Child, TRUE);
            }
        }
    }

    XHCI_TeardownDeviceTransfers(Device, UsbDevice);
    XHCI_FreeUsbDeviceResources(UsbDevice);

    if (FreeSelf) {
        XHCI_ReleaseUsbDevice(UsbDevice);
    }
}

/************************************************************************/

/**
 * @brief Convert an xHCI speed ID to a human readable name.
 * @param SpeedId Raw PORTSC speed value.
 * @return Speed string.
 */
LPCSTR XHCI_SpeedToString(U32 SpeedId) {
    switch (SpeedId) {
        case 1:
            return TEXT("FS");
        case 2:
            return TEXT("LS");
        case 3:
            return TEXT("HS");
        case 4:
            return TEXT("SS");
        case 5:
            return TEXT("SS+");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/

/**
 * @brief Convert endpoint address to xHCI DCI.
 * @param EndpointAddress USB endpoint address.
 * @return DCI index.
 */
static U8 XHCI_GetEndpointDci(U8 EndpointAddress) {
    U8 EndpointNumber = EndpointAddress & 0x0F;
    U8 DirectionIn = (EndpointAddress & 0x80) != 0 ? 1U : 0U;
    return (U8)((EndpointNumber * 2U) + DirectionIn);
}

/************************************************************************/

/**
 * @brief Get the selected configuration for a device.
 * @param UsbDevice USB device state.
 * @return Pointer to configuration or NULL.
 */
LPXHCI_USB_CONFIGURATION XHCI_GetSelectedConfig(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL || UsbDevice->Configs == NULL || UsbDevice->ConfigCount == 0) {
        return NULL;
    }

    if (UsbDevice->SelectedConfigValue == 0) {
        return &UsbDevice->Configs[0];
    }

    for (UINT Index = 0; Index < UsbDevice->ConfigCount; Index++) {
        if (UsbDevice->Configs[Index].ConfigurationValue == UsbDevice->SelectedConfigValue) {
            return &UsbDevice->Configs[Index];
        }
    }

    return &UsbDevice->Configs[0];
}

/************************************************************************/

/**
 * @brief Detect whether a USB device is a hub.
 * @param UsbDevice USB device state.
 * @return TRUE when the device exposes the hub class.
 */
static BOOL XHCI_IsHubDevice(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    if (UsbDevice->DeviceDescriptor.DeviceClass == USB_CLASS_HUB) {
        return TRUE;
    }

    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return FALSE;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass == USB_CLASS_HUB) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Locate an endpoint in an interface by type and direction.
 * @param Interface USB interface.
 * @param EndpointType Endpoint type.
 * @param DirectionIn TRUE for IN endpoints, FALSE for OUT.
 * @return Endpoint pointer or NULL.
 */
LPXHCI_USB_ENDPOINT XHCI_FindInterfaceEndpoint(LPXHCI_USB_INTERFACE Interface, U8 EndpointType, BOOL DirectionIn) {
    if (Interface == NULL) {
        return NULL;
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
        if (Endpoint->Parent != (LPLISTNODE)Interface) {
            continue;
        }
        if ((Endpoint->Attributes & 0x03) != EndpointType) {
            continue;
        }
        if (DirectionIn) {
            if ((Endpoint->Address & 0x80) == 0) {
                continue;
            }
        } else {
            if ((Endpoint->Address & 0x80) != 0) {
                continue;
            }
        }

        return Endpoint;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Locate the interrupt IN endpoint for a hub device.
 * @param UsbDevice USB device state.
 * @return Endpoint pointer or NULL.
 */
LPXHCI_USB_ENDPOINT XHCI_FindHubInterruptEndpoint(LPXHCI_USB_DEVICE UsbDevice) {
    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return NULL;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass != USB_CLASS_HUB) {
            continue;
        }

        return XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_INTERRUPT, TRUE);
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initialize a transfer ring.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical base.
 * @param LinearOut Receives linear base.
 * @param CycleStateOut Receives cycle state.
 * @param EnqueueIndexOut Receives enqueue index.
 * @return TRUE on success.
 */
static BOOL XHCI_InitTransferRingCore(LPCSTR Tag, PHYSICAL* PhysicalOut, LINEAR* LinearOut,
                                      U32* CycleStateOut, U32* EnqueueIndexOut) {
    if (PhysicalOut == NULL || LinearOut == NULL || CycleStateOut == NULL || EnqueueIndexOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(Tag, PhysicalOut, LinearOut)) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)(*LinearOut);
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(*PhysicalOut);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    *CycleStateOut = 1;
    *EnqueueIndexOut = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize an endpoint transfer ring.
 * @param Endpoint Endpoint descriptor.
 * @param Tag Allocation tag.
 * @return TRUE on success.
 */
static BOOL XHCI_InitEndpointRing(LPXHCI_USB_ENDPOINT Endpoint, LPCSTR Tag) {
    if (Endpoint == NULL) {
        return FALSE;
    }

    return XHCI_InitTransferRingCore(Tag,
                                     &Endpoint->TransferRingPhysical,
                                     &Endpoint->TransferRingLinear,
                                     &Endpoint->TransferRingCycleState,
                                     &Endpoint->TransferRingEnqueueIndex);
}

/************************************************************************/

typedef BOOL (*XHCI_DESC_CALLBACK)(const U8* Descriptor, U8 Length, void* Context);

/**
 * @brief Walk all descriptors in a configuration buffer.
 * @param Buffer Descriptor buffer.
 * @param Length Buffer length.
 * @param Callback Callback invoked per descriptor.
 * @param Context Callback context.
 * @return TRUE on success.
 */
static BOOL XHCI_ForEachDescriptor(const U8* Buffer, U16 Length, XHCI_DESC_CALLBACK Callback, void* Context) {
    U16 Offset = 0;

    if (Buffer == NULL || Callback == NULL) {
        return FALSE;
    }

    while ((Offset + 2) <= Length) {
        U8 DescLength = Buffer[Offset];
        U8 DescType = Buffer[Offset + 1];
        const U8* Desc = &Buffer[Offset];

        if (DescLength < 2 || (Offset + DescLength) > Length) {
            DEBUG(TEXT("[XHCI_ForEachDescriptor] Invalid descriptor length=%u type=%u"), DescLength, DescType);
            return FALSE;
        }

        if (!Callback(Desc, DescLength, Context)) {
            return FALSE;
        }

        Offset = (U16)(Offset + DescLength);
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_COUNT_CONFIG {
    UINT ConfigCount;
} XHCI_DESC_COUNT_CONFIG, *LPXHCI_DESC_COUNT_CONFIG;

static BOOL XHCI_CountConfigCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_COUNT_CONFIG Ctx = (LPXHCI_DESC_COUNT_CONFIG)Context;
    UNUSED(Length);

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
        Ctx->ConfigCount++;
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_FILL_CONTEXT {
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_CONFIGURATION Configs;
    UINT ConfigCount;
    UINT ConfigIndex;
    LPXHCI_USB_CONFIGURATION CurrentConfig;
    LPXHCI_USB_INTERFACE CurrentInterface;
} XHCI_DESC_FILL_CONTEXT, *LPXHCI_DESC_FILL_CONTEXT;

static BOOL XHCI_FillDescriptorCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_FILL_CONTEXT Ctx = (LPXHCI_DESC_FILL_CONTEXT)Context;

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
        if (Length < sizeof(USB_CONFIGURATION_DESCRIPTOR) || Ctx->ConfigIndex >= Ctx->ConfigCount) {
            return TRUE;
        }

        const USB_CONFIGURATION_DESCRIPTOR* ConfigDesc = (const USB_CONFIGURATION_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_CONFIGURATION Config = &Ctx->Configs[Ctx->ConfigIndex];

        Config->ConfigurationValue = ConfigDesc->ConfigurationValue;
        Config->ConfigurationIndex = ConfigDesc->ConfigurationIndex;
        Config->Attributes = ConfigDesc->Attributes;
        Config->MaxPower = ConfigDesc->MaxPower;
        Config->NumInterfaces = ConfigDesc->NumInterfaces;
        Config->TotalLength = ConfigDesc->TotalLength;
        Config->InterfaceCount = 0;

        Ctx->CurrentConfig = Config;
        Ctx->CurrentInterface = NULL;
        Ctx->ConfigIndex++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
        if (Length < sizeof(USB_INTERFACE_DESCRIPTOR) || Ctx->CurrentConfig == NULL) {
            return TRUE;
        }

        const USB_INTERFACE_DESCRIPTOR* IfDesc = (const USB_INTERFACE_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_INTERFACE Interface =
            (LPXHCI_USB_INTERFACE)CreateKernelObject(sizeof(XHCI_USB_INTERFACE), KOID_USBINTERFACE);
        if (Interface == NULL) {
            ERROR(TEXT("[XHCI_FillDescriptorCallback] Interface allocation failed"));
            return FALSE;
        }
        MemorySet((U8*)Interface + sizeof(LISTNODE), 0, sizeof(XHCI_USB_INTERFACE) - sizeof(LISTNODE));

        Interface->ConfigurationValue = Ctx->CurrentConfig->ConfigurationValue;
        Interface->Number = IfDesc->InterfaceNumber;
        Interface->AlternateSetting = IfDesc->AlternateSetting;
        Interface->NumEndpoints = IfDesc->NumEndpoints;
        Interface->InterfaceClass = IfDesc->InterfaceClass;
        Interface->InterfaceSubClass = IfDesc->InterfaceSubClass;
        Interface->InterfaceProtocol = IfDesc->InterfaceProtocol;
        Interface->InterfaceIndex = IfDesc->InterfaceIndex;
        Interface->EndpointCount = 0;

        LPLIST InterfaceList = GetUsbInterfaceList();
        if (InterfaceList == NULL || !ListAddItemWithParent(InterfaceList, Interface, (LPLISTNODE)Ctx->UsbDevice)) {
            ReleaseKernelObject(Interface);
            return FALSE;
        }

        Ctx->CurrentInterface = Interface;
        Ctx->CurrentConfig->InterfaceCount++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
        if (Length < sizeof(USB_ENDPOINT_DESCRIPTOR) || Ctx->CurrentInterface == NULL) {
            return TRUE;
        }

        const USB_ENDPOINT_DESCRIPTOR* EpDesc = (const USB_ENDPOINT_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_ENDPOINT Endpoint =
            (LPXHCI_USB_ENDPOINT)CreateKernelObject(sizeof(XHCI_USB_ENDPOINT), KOID_USBENDPOINT);
        if (Endpoint == NULL) {
            ERROR(TEXT("[XHCI_FillDescriptorCallback] Endpoint allocation failed"));
            return FALSE;
        }
        MemorySet((U8*)Endpoint + sizeof(LISTNODE), 0, sizeof(XHCI_USB_ENDPOINT) - sizeof(LISTNODE));

        Endpoint->Address = EpDesc->EndpointAddress;
        Endpoint->Attributes = EpDesc->Attributes;
        Endpoint->MaxPacketSize = EpDesc->MaxPacketSize;
        Endpoint->Interval = EpDesc->Interval;

        LPLIST EndpointList = GetUsbEndpointList();
        if (EndpointList == NULL || !ListAddItemWithParent(EndpointList, Endpoint, (LPLISTNODE)Ctx->CurrentInterface)) {
            ReleaseKernelObject(Endpoint);
            return FALSE;
        }

        Ctx->CurrentInterface->EndpointCount++;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse configuration descriptor and build the USB tree.
 * @param UsbDevice USB device state.
 * @param Buffer Descriptor buffer.
 * @param Length Buffer length.
 * @return TRUE on success.
 */
static BOOL XHCI_ParseConfigDescriptor(LPXHCI_USB_DEVICE UsbDevice, const U8* Buffer, U16 Length) {
    XHCI_DESC_COUNT_CONFIG ConfigCountContext;
    XHCI_DESC_FILL_CONTEXT FillContext;

    if (UsbDevice == NULL || Buffer == NULL || Length == 0) {
        return FALSE;
    }

    XHCI_FreeUsbTree(UsbDevice);

    MemorySet(&ConfigCountContext, 0, sizeof(ConfigCountContext));
    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_CountConfigCallback, &ConfigCountContext)) {
        return FALSE;
    }

    if (ConfigCountContext.ConfigCount == 0) {
        return FALSE;
    }

    UsbDevice->Configs =
        (LPXHCI_USB_CONFIGURATION)KernelHeapAlloc(sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    if (UsbDevice->Configs == NULL) {
        return FALSE;
    }
    MemorySet(UsbDevice->Configs, 0, sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    UsbDevice->ConfigCount = ConfigCountContext.ConfigCount;

    MemorySet(&FillContext, 0, sizeof(FillContext));
    FillContext.UsbDevice = UsbDevice;
    FillContext.Configs = UsbDevice->Configs;
    FillContext.ConfigCount = UsbDevice->ConfigCount;

    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_FillDescriptorCallback, &FillContext)) {
        XHCI_FreeUsbTree(UsbDevice);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Get default EP0 max packet size for a speed.
 * @param SpeedId xHCI speed ID.
 * @return Max packet size.
 */
static U16 XHCI_GetDefaultMaxPacketSize0(U8 SpeedId) {
    switch (SpeedId) {
        case 1:
        case 2:
            return 8;
        case 3:
            return 64;
        case 4:
        case 5:
            return 512;
        default:
            return 8;
    }
}

/************************************************************************/

/**
 * @brief Compute EP0 max packet size from descriptor data.
 * @param SpeedId xHCI speed ID.
 * @param DescriptorValue bMaxPacketSize0 value.
 * @return Max packet size.
 */
static U16 XHCI_ComputeMaxPacketSize0(U8 SpeedId, U8 DescriptorValue) {
    if (SpeedId == 4 || SpeedId == 5) {
        if (DescriptorValue < 5 || DescriptorValue > 10) {
            return 512;
        }
        return (U16)(1U << DescriptorValue);
    }
    return DescriptorValue;
}

/************************************************************************/

/**
 * @brief Reset a port and wait for completion.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ResetPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_CCS) == 0) {
        return FALSE;
    }

    PortStatus |= XHCI_PORTSC_PR;
    PortStatus &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, PortStatus);

    if (!XHCI_WaitForRegister(Device->OpBase, Offset, XHCI_PORTSC_PR, 0, XHCI_PORT_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetPort] Port %u reset timeout"), PortIndex + 1);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL XHCI_InitTransferRing(LPXHCI_USB_DEVICE UsbDevice) {
    return XHCI_InitTransferRingCore(TEXT("XHCI_TransferRing"),
                                     &UsbDevice->TransferRingPhysical,
                                     &UsbDevice->TransferRingLinear,
                                     &UsbDevice->TransferRingCycleState,
                                     &UsbDevice->TransferRingEnqueueIndex);
}

/************************************************************************/

/**
 * @brief Initialize USB device state for a port.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_InitUsbDeviceState(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (XHCI_UsbTreeHasReferences(UsbDevice)) {
        WARNING(TEXT("[XHCI_InitUsbDeviceState] Device still referenced, skipping reset"));
        return FALSE;
    }

    XHCI_FreeUsbTree(UsbDevice);
    if (UsbDevice->InputContextLinear) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        UsbDevice->InputContextLinear = 0;
    }
    if (UsbDevice->InputContextPhysical) {
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextPhysical = 0;
    }
    if (UsbDevice->DeviceContextLinear) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        UsbDevice->DeviceContextLinear = 0;
    }
    if (UsbDevice->DeviceContextPhysical) {
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        UsbDevice->DeviceContextPhysical = 0;
    }
    if (UsbDevice->TransferRingLinear) {
        FreeRegion(UsbDevice->TransferRingLinear, PAGE_SIZE);
        UsbDevice->TransferRingLinear = 0;
    }
    if (UsbDevice->TransferRingPhysical) {
        FreePhysicalPage(UsbDevice->TransferRingPhysical);
        UsbDevice->TransferRingPhysical = 0;
    }
    if (UsbDevice->HubStatusLinear) {
        FreeRegion(UsbDevice->HubStatusLinear, PAGE_SIZE);
        UsbDevice->HubStatusLinear = 0;
    }
    if (UsbDevice->HubStatusPhysical) {
        FreePhysicalPage(UsbDevice->HubStatusPhysical);
        UsbDevice->HubStatusPhysical = 0;
    }
    if (UsbDevice->HubChildren != NULL) {
        KernelHeapFree(UsbDevice->HubChildren);
        UsbDevice->HubChildren = NULL;
    }
    if (UsbDevice->HubPortStatus != NULL) {
        KernelHeapFree(UsbDevice->HubPortStatus);
        UsbDevice->HubPortStatus = NULL;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_InputContext"), &UsbDevice->InputContextPhysical, &UsbDevice->InputContextLinear)) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DeviceContext"), &UsbDevice->DeviceContextPhysical, &UsbDevice->DeviceContextLinear)) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextLinear = 0;
        UsbDevice->InputContextPhysical = 0;
        return FALSE;
    }

    if (!XHCI_InitTransferRing(UsbDevice)) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->DeviceContextLinear = 0;
        UsbDevice->DeviceContextPhysical = 0;
        UsbDevice->InputContextLinear = 0;
        UsbDevice->InputContextPhysical = 0;
        return FALSE;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    MemorySet((LPVOID)UsbDevice->DeviceContextLinear, 0, PAGE_SIZE);
    UsbDevice->Present = FALSE;
    UsbDevice->SlotId = 0;
    UsbDevice->Address = 0;
    UsbDevice->SelectedConfigValue = 0;
    UsbDevice->StringManufacturer = 0;
    UsbDevice->StringProduct = 0;
    UsbDevice->StringSerial = 0;
    UsbDevice->IsHub = FALSE;
    UsbDevice->HubPortCount = 0;
    UsbDevice->HubInterruptEndpoint = NULL;
    UsbDevice->HubInterruptLength = 0;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);
    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->DestroyPending = FALSE;
    UsbDevice->Controller = Device;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Populate an input context for Address Device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_BuildInputContextForAddress(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << 1);

    LPXHCI_CONTEXT_32 Slot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    Slot->Dword0 = (UsbDevice->RouteString & XHCI_SLOT_CTX_ROUTE_STRING_MASK) |
                   ((U32)UsbDevice->SpeedId << XHCI_SLOT_CTX_SPEED_SHIFT) |
                   (1U << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    if (UsbDevice->IsHub) {
        Slot->Dword0 |= XHCI_SLOT_CTX_HUB;
    }

    Slot->Dword1 = ((U32)UsbDevice->RootPortNumber << XHCI_SLOT_CTX_ROOT_PORT_SHIFT);
    if (UsbDevice->IsHub && UsbDevice->HubPortCount != 0) {
        Slot->Dword1 |= ((U32)UsbDevice->HubPortCount << XHCI_SLOT_CTX_PORT_COUNT_SHIFT);
    }

    LPXHCI_USB_DEVICE Parent = (LPXHCI_USB_DEVICE)UsbDevice->Parent;
    if (Parent != NULL) {
        if ((Parent->SpeedId == USB_SPEED_HS) &&
            (UsbDevice->SpeedId == USB_SPEED_LS || UsbDevice->SpeedId == USB_SPEED_FS)) {
            Slot->Dword2 = ((U32)Parent->SlotId << XHCI_SLOT_CTX_TT_HUB_SLOT_SHIFT) |
                           ((U32)UsbDevice->ParentPort << XHCI_SLOT_CTX_TT_PORT_SHIFT);
        }
    }

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = (4U << 3) | ((U32)UsbDevice->MaxPacketSize0 << 16);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Populate an input context for updating EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_BuildInputContextForEp0(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 1);

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = (4U << 3) | ((U32)UsbDevice->MaxPacketSize0 << 16);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Enable a new device slot.
 * @param Device xHCI device.
 * @param SlotIdOut Receives allocated slot ID.
 * @return TRUE on success.
 */
static BOOL XHCI_EnableSlot(LPXHCI_DEVICE Device, U8* SlotIdOut) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U8 SlotId = 0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, &SlotId, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_EnableSlot] Completion code %u"), Completion);
        return FALSE;
    }

    if (SlotIdOut != NULL) {
        *SlotIdOut = SlotId;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Address a device with a prepared input context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_AddressDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_AddressDevice] Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Evaluate context to update EP0 parameters.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_EvaluateContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_EVALUATE_CONTEXT << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_EvaluateContext] Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Configure endpoint contexts after a SET_CONFIGURATION.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_ConfigureEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_ConfigureEndpoint] Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Add an interrupt IN endpoint to the device context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return TRUE on success.
 */
BOOL XHCI_AddInterruptEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (Endpoint->TransferRingLinear == 0 || Endpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(Endpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }

    Endpoint->Dci = XHCI_GetEndpointDci(Endpoint->Address);

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << Endpoint->Dci);

    LPVOID SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    LPVOID SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);

    {
        LPXHCI_CONTEXT_32 Slot = (LPXHCI_CONTEXT_32)SlotOut;
        U32 ContextEntries = (U32)Endpoint->Dci + 1U;
        Slot->Dword0 &= ~(0x1FU << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
        Slot->Dword0 |= ((ContextEntries & 0x1FU) << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    }

    LPXHCI_CONTEXT_32 EpCtx = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, (U32)Endpoint->Dci + 1U);
    U32 EpType = 0;
    if ((Endpoint->Attributes & 0x03) == USB_ENDPOINT_TYPE_INTERRUPT) {
        EpType = ((Endpoint->Address & 0x80) != 0) ? 7U : 3U;
    }
    U32 IntervalField = Endpoint->Interval;
    if (IntervalField == 0) {
        IntervalField = 1;
    }
    if (UsbDevice->SpeedId == USB_SPEED_HS || UsbDevice->SpeedId == USB_SPEED_SS) {
        if (IntervalField > 0) {
            IntervalField -= 1;
        }
    }
    if (IntervalField > 255) {
        IntervalField = 255;
    }

    U32 MaxPacket = ((U32)Endpoint->MaxPacketSize & 0x7FFU);

    EpCtx->Dword0 = (IntervalField << 16);
    EpCtx->Dword1 = (3U) | ((EpType << 3) | (MaxPacket << 16));

    {
        U64 Dequeue = U64_FromUINT(Endpoint->TransferRingPhysical);
        EpCtx->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        EpCtx->Dword2 |= (Endpoint->TransferRingCycleState ? 1U : 0U);
        EpCtx->Dword3 = U64_High32(Dequeue);
        EpCtx->Dword4 = MaxPacket;
    }

    DEBUG(TEXT("[XHCI_AddInterruptEndpoint] Slot=%x DCI=%x Speed=%x EpAddr=%x Attr=%x Interval=%x Field=%x MaxPkt=%x Dequeue=%x:%x"),
          (U32)UsbDevice->SlotId,
          (U32)Endpoint->Dci,
          (U32)UsbDevice->SpeedId,
          (U32)Endpoint->Address,
          (U32)Endpoint->Attributes,
          (U32)Endpoint->Interval,
          (U32)IntervalField,
          (U32)MaxPacket,
          (U32)EpCtx->Dword3,
          (U32)EpCtx->Dword2);
    DEBUG(TEXT("[XHCI_AddInterruptEndpoint] CtrlAdd=%x SlotD0=%x SlotD1=%x EpD0=%x EpD1=%x EpD2=%x EpD3=%x EpD4=%x"),
          (U32)Control->Dword1,
          (U32)((LPXHCI_CONTEXT_32)SlotOut)->Dword0,
          (U32)((LPXHCI_CONTEXT_32)SlotOut)->Dword1,
          (U32)EpCtx->Dword0,
          (U32)EpCtx->Dword1,
          (U32)EpCtx->Dword2,
          (U32)EpCtx->Dword3,
          (U32)EpCtx->Dword4);

    return XHCI_ConfigureEndpoint(Device, UsbDevice);
}

/************************************************************************/

/**
 * @brief Add a bulk endpoint to the device context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return TRUE on success.
 */
BOOL XHCI_AddBulkEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (Endpoint->TransferRingLinear == 0 || Endpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(Endpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }

    Endpoint->Dci = XHCI_GetEndpointDci(Endpoint->Address);

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << Endpoint->Dci);

    LPVOID SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    LPVOID SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);

    {
        LPXHCI_CONTEXT_32 Slot = (LPXHCI_CONTEXT_32)SlotOut;
        U32 ContextEntries = (U32)Endpoint->Dci + 1U;
        Slot->Dword0 &= ~(0x1FU << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
        Slot->Dword0 |= ((ContextEntries & 0x1FU) << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    }

    LPXHCI_CONTEXT_32 EpCtx = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, (U32)Endpoint->Dci + 1U);
    U32 EpType = ((Endpoint->Address & 0x80) != 0) ? 6U : 2U;
    U32 MaximumPacketSize = ((U32)Endpoint->MaxPacketSize & 0x7FFU);

    EpCtx->Dword0 = 0;
    EpCtx->Dword1 = (3U) | ((EpType << 3) | (MaximumPacketSize << 16));

    {
        U64 Dequeue = U64_FromUINT(Endpoint->TransferRingPhysical);
        EpCtx->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        EpCtx->Dword2 |= (Endpoint->TransferRingCycleState ? 1U : 0U);
        EpCtx->Dword3 = U64_High32(Dequeue);
        EpCtx->Dword4 = MaximumPacketSize;
    }

    DEBUG(TEXT("[XHCI_AddBulkEndpoint] Slot=%x DCI=%x Speed=%x EpAddr=%x Attr=%x MaxPacketSize=%u Dequeue=%x:%x"),
          (U32)UsbDevice->SlotId,
          (U32)Endpoint->Dci,
          (U32)UsbDevice->SpeedId,
          (U32)Endpoint->Address,
          (U32)Endpoint->Attributes,
          (U32)MaximumPacketSize,
          (U32)EpCtx->Dword3,
          (U32)EpCtx->Dword2);
    DEBUG(TEXT("[XHCI_AddBulkEndpoint] CtrlAdd=%x SlotD0=%x SlotD1=%x EpD0=%x EpD1=%x EpD2=%x EpD3=%x EpD4=%x"),
          (U32)Control->Dword1,
          (U32)((LPXHCI_CONTEXT_32)SlotOut)->Dword0,
          (U32)((LPXHCI_CONTEXT_32)SlotOut)->Dword1,
          (U32)EpCtx->Dword0,
          (U32)EpCtx->Dword1,
          (U32)EpCtx->Dword2,
          (U32)EpCtx->Dword3,
          (U32)EpCtx->Dword4);

    return XHCI_ConfigureEndpoint(Device, UsbDevice);
}

/************************************************************************/

/**
 * @brief Update slot context for hub information.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_UpdateHubSlotContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0);

    LPXHCI_CONTEXT_32 Slot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    Slot->Dword0 = (UsbDevice->RouteString & XHCI_SLOT_CTX_ROUTE_STRING_MASK) |
                   ((U32)UsbDevice->SpeedId << XHCI_SLOT_CTX_SPEED_SHIFT) |
                   XHCI_SLOT_CTX_HUB |
                   (1U << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    Slot->Dword1 = ((U32)UsbDevice->RootPortNumber << XHCI_SLOT_CTX_ROOT_PORT_SHIFT) |
                   ((U32)UsbDevice->HubPortCount << XHCI_SLOT_CTX_PORT_COUNT_SHIFT);

    return XHCI_EvaluateContext(Device, UsbDevice);
}

/************************************************************************/

/**
 * @brief Perform a control transfer on EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Setup Setup packet.
 * @param Buffer Data buffer (optional).
 * @param Length Data length.
 * @param DirectionIn TRUE if data is IN.
 * @return TRUE on success.
 */
BOOL XHCI_ControlTransfer(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, const USB_SETUP_PACKET* Setup,
                                 PHYSICAL BufferPhysical, LPVOID BufferLinear, U16 Length, BOOL DirectionIn) {
    XHCI_TRB SetupTrb;
    XHCI_TRB DataTrb;
    XHCI_TRB StatusTrb;
    U64 StatusPhysical = U64_0;
    U32 Completion = 0;

    if (Setup == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet(&SetupTrb, 0, sizeof(SetupTrb));
    MemoryCopy(&SetupTrb.Dword0, Setup, sizeof(USB_SETUP_PACKET));
    SetupTrb.Dword2 = 8;
    SetupTrb.Dword3 = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT;

    if (!XHCI_TransferRingEnqueue(UsbDevice, &SetupTrb, NULL)) {
        return FALSE;
    }

    if (Length > 0 && BufferLinear != NULL && BufferPhysical != 0) {
        MemorySet(&DataTrb, 0, sizeof(DataTrb));
        DataTrb.Dword0 = U64_Low32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword1 = U64_High32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword2 = Length;
        DataTrb.Dword3 = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);
        if (DirectionIn) {
            DataTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }

        if (!XHCI_TransferRingEnqueue(UsbDevice, &DataTrb, NULL)) {
            return FALSE;
        }
    }

    MemorySet(&StatusTrb, 0, sizeof(StatusTrb));
    StatusTrb.Dword3 = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (Length > 0) {
        if (!DirectionIn) {
            StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }
    } else {
        StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
    }

    if (!XHCI_TransferRingEnqueue(UsbDevice, &StatusTrb, &StatusPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, UsbDevice->SlotId, XHCI_EP0_DCI);

    if (!XHCI_WaitForTransferCompletion(Device, StatusPhysical, &Completion)) {
        return FALSE;
    }

    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        return TRUE;
    }

    if (Completion == XHCI_COMPLETION_STALL_ERROR) {
        USB_SETUP_PACKET ClearFeature;
        MemorySet(&ClearFeature, 0, sizeof(ClearFeature));
        ClearFeature.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_ENDPOINT;
        ClearFeature.Request = USB_REQUEST_CLEAR_FEATURE;
        ClearFeature.Value = USB_FEATURE_ENDPOINT_HALT;
        ClearFeature.Index = 0;
        ClearFeature.Length = 0;
        (void)XHCI_ControlTransfer(Device, UsbDevice, &ClearFeature, 0, NULL, 0, FALSE);
    }

    ERROR(TEXT("[XHCI_ControlTransfer] Completion code %u"), Completion);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Read the full configuration descriptor.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @param LengthOut Receives total length.
 * @return TRUE on success.
 */
static BOOL XHCI_ReadConfigDescriptor(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice,
                                      PHYSICAL* PhysicalOut, LINEAR* LinearOut, U16* LengthOut) {
    USB_SETUP_PACKET Setup;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;
    USB_CONFIGURATION_DESCRIPTOR Config;
    U16 TotalLength = 0;

    if (PhysicalOut == NULL || LinearOut == NULL || LengthOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_CfgDesc"), &Physical, &Linear)) {
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, USB_DESCRIPTOR_LENGTH_CONFIGURATION);

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_DESCRIPTOR_TYPE_CONFIGURATION << 8);
    Setup.Index = 0;
    Setup.Length = USB_DESCRIPTOR_LENGTH_CONFIGURATION;

    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, (LPVOID)Linear,
                              USB_DESCRIPTOR_LENGTH_CONFIGURATION, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemoryCopy(&Config, (LPVOID)Linear, sizeof(Config));
    TotalLength = Config.TotalLength;
    if (TotalLength == 0) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    if (TotalLength > PAGE_SIZE) {
        DEBUG(TEXT("[XHCI_ReadConfigDescriptor] Truncated config descriptor %u -> %u"), TotalLength, PAGE_SIZE);
        TotalLength = PAGE_SIZE;
    }

    MemorySet((LPVOID)Linear, 0, TotalLength);
    Setup.Length = TotalLength;
    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, (LPVOID)Linear, TotalLength, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    *LengthOut = TotalLength;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Get the USB device descriptor.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_GetDeviceDescriptor(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    USB_SETUP_PACKET Setup;
    LPVOID Buffer = NULL;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;

    if (!XHCI_AllocPage(TEXT("XHCI_DevDesc"), &Physical, &Linear)) {
        return FALSE;
    }

    Buffer = (LPVOID)Linear;
    MemorySet(Buffer, 0, USB_DESCRIPTOR_LENGTH_DEVICE);

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_DESCRIPTOR_TYPE_DEVICE << 8);
    Setup.Index = 0;
    Setup.Length = USB_DESCRIPTOR_LENGTH_DEVICE;

    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, Buffer, USB_DESCRIPTOR_LENGTH_DEVICE, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemoryCopy(&UsbDevice->DeviceDescriptor, Buffer, sizeof(USB_DEVICE_DESCRIPTOR));

    FreeRegion(Linear, PAGE_SIZE);
    FreePhysicalPage(Physical);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate a USB device already reset on a given port.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_EnumerateDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    PHYSICAL ConfigPhysical = 0;
    LINEAR ConfigLinear = 0;
    U16 ConfigLength = 0;

    UsbDevice->MaxPacketSize0 = XHCI_GetDefaultMaxPacketSize0(UsbDevice->SpeedId);

    if (!XHCI_InitUsbDeviceState(Device, UsbDevice)) {
        return FALSE;
    }

    if (!XHCI_EnableSlot(Device, &UsbDevice->SlotId)) {
        return FALSE;
    }

    ((U64 *)Device->DcbaaLinear)[UsbDevice->SlotId] = U64_FromUINT(UsbDevice->DeviceContextPhysical);

    XHCI_BuildInputContextForAddress(Device, UsbDevice);
    if (!XHCI_AddressDevice(Device, UsbDevice)) {
        return FALSE;
    }

    UsbDevice->Address = UsbDevice->SlotId;

    if (!XHCI_GetDeviceDescriptor(Device, UsbDevice)) {
        return FALSE;
    }

    UsbDevice->StringManufacturer = UsbDevice->DeviceDescriptor.ManufacturerIndex;
    UsbDevice->StringProduct = UsbDevice->DeviceDescriptor.ProductIndex;
    UsbDevice->StringSerial = UsbDevice->DeviceDescriptor.SerialNumberIndex;

    UsbDevice->MaxPacketSize0 = XHCI_ComputeMaxPacketSize0(
        UsbDevice->SpeedId, UsbDevice->DeviceDescriptor.MaxPacketSize0);

    XHCI_BuildInputContextForEp0(Device, UsbDevice);
    (void)XHCI_EvaluateContext(Device, UsbDevice);

    if (!XHCI_ReadConfigDescriptor(Device, UsbDevice, &ConfigPhysical, &ConfigLinear, &ConfigLength)) {
        return FALSE;
    }

    if (!XHCI_ParseConfigDescriptor(UsbDevice, (const U8*)ConfigLinear, ConfigLength)) {
        FreeRegion(ConfigLinear, PAGE_SIZE);
        FreePhysicalPage(ConfigPhysical);
        return FALSE;
    }

    if (ConfigLinear) {
        FreeRegion(ConfigLinear, PAGE_SIZE);
        FreePhysicalPage(ConfigPhysical);
    }

    if (UsbDevice->ConfigCount > 0) {
        USB_SETUP_PACKET Setup;
        MemorySet(&Setup, 0, sizeof(Setup));
        Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
        Setup.Request = USB_REQUEST_SET_CONFIGURATION;
        Setup.Value = UsbDevice->Configs[0].ConfigurationValue;
        Setup.Index = 0;
        Setup.Length = 0;

        if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE)) {
            return FALSE;
        }

        UsbDevice->SelectedConfigValue = UsbDevice->Configs[0].ConfigurationValue;
    }

    UsbDevice->IsHub = XHCI_IsHubDevice(UsbDevice);
    UsbDevice->Present = TRUE;
    XHCI_AddDeviceToList(Device, UsbDevice);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Probe a port and fetch descriptors.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PortIndex Port index (0-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ProbePort(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U32 PortIndex) {
    U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
    U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

    if ((PortStatus & XHCI_PORTSC_CCS) == 0) {
        UsbDevice->Present = FALSE;
        return FALSE;
    }

    if (UsbDevice->DestroyPending && XHCI_UsbTreeHasReferences(UsbDevice)) {
        WARNING(TEXT("[XHCI_ProbePort] Port %u still referenced, delaying re-enumeration"),
                PortIndex + 1);
        return FALSE;
    }

    UsbDevice->PortNumber = (U8)(PortIndex + 1);
    UsbDevice->RootPortNumber = UsbDevice->PortNumber;
    UsbDevice->Depth = 0;
    UsbDevice->RouteString = 0;
    UsbDevice->Parent = NULL;
    UsbDevice->ParentPort = 0;
    UsbDevice->IsRootPort = TRUE;
    UsbDevice->Controller = Device;
    UsbDevice->SpeedId = (U8)SpeedId;
    UsbDevice->DestroyPending = FALSE;

    if (UsbDevice->Present) {
        return TRUE;
    }

    if (!XHCI_ResetPort(Device, PortIndex)) {
        return FALSE;
    }

    if (!XHCI_EnumerateDevice(Device, UsbDevice)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u enumerate failed"), PortIndex + 1);
        return FALSE;
    }

    DEBUG(TEXT("[XHCI_ProbePort] Port %u VID=%x PID=%x"),
          PortIndex + 1,
          UsbDevice->DeviceDescriptor.VendorID,
          UsbDevice->DeviceDescriptor.ProductID);

    DEBUG(TEXT("[XHCI_ProbePort] Port %u Configs=%u SelectedConfig=%u"),
          PortIndex + 1,
          UsbDevice->ConfigCount,
          UsbDevice->SelectedConfigValue);

    if (UsbDevice->IsHub) {
        if (!XHCI_InitHub(Device, UsbDevice)) {
            ERROR(TEXT("[XHCI_ProbePort] Port %u hub init failed"), PortIndex + 1);
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate connected devices on all ports.
 * @param Device xHCI device.
 */
void XHCI_EnsureUsbDevices(LPXHCI_DEVICE Device) {
    if (Device == NULL || Device->UsbDevices == NULL) {
        return;
    }

    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
        if (UsbDevice == NULL) {
            continue;
        }
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        if (!Connected) {
            if (UsbDevice->Present) {
                XHCI_DestroyUsbDevice(Device, UsbDevice, FALSE);
            }
            continue;
        }

        if (!UsbDevice->Present) {
            (void)XHCI_ProbePort(Device, UsbDevice, PortIndex);
        }
    }
}
