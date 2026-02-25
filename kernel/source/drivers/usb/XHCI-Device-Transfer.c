
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

\************************************************************************/

#include "drivers/usb/XHCI-Internal.h"
#include "drivers/usb/XHCI-Device-Internal.h"
#include "Clock.h"
#include "utils/ThresholdLatch.h"

/************************************************************************/

#define XHCI_ENUM_FAILURE_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENUM_FAILURE_LOG_INTERVAL_MS 2000
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_INTERVAL_MS 2000

/************************************************************************/

/**
 * @brief Count active slots attached to one controller.
 * @param Device xHCI controller.
 * @return Number of active slot identifiers.
 */
static U32 XHCI_CountActiveSlots(LPXHCI_DEVICE Device) {
    U8 SlotSeen[256];
    U32 ActiveCount = 0;
    LPLIST UsbDeviceList;

    if (Device == NULL) {
        return 0;
    }

    MemorySet(SlotSeen, 0, sizeof(SlotSeen));

    UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return 0;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)Node;
        if (UsbDevice->Controller != Device) {
            continue;
        }
        if (!UsbDevice->Present || UsbDevice->SlotId == 0) {
            continue;
        }
        if (SlotSeen[UsbDevice->SlotId] != 0) {
            continue;
        }

        SlotSeen[UsbDevice->SlotId] = 1;
        ActiveCount++;
    }

    return ActiveCount;
}

/************************************************************************/

/**
 * @brief Emit one rate-limited state snapshot for EnableSlot timeout.
 * @param Device xHCI controller.
 */
static void XHCI_LogEnableSlotTimeoutState(LPXHCI_DEVICE Device) {
    static RATE_LIMITER DATA_SECTION EnableSlotTimeoutLimiter = {0};
    static BOOL DATA_SECTION EnableSlotTimeoutLimiterInitAttempted = FALSE;
    U32 Suppressed = 0;
    LINEAR InterrupterBase;
    U32 UsbStatus;
    U32 UsbCommand;
    U32 CrcrLow;
    U32 CrcrHigh;
    U32 Iman;
    U32 ErdpLow;
    U32 ErdpHigh;
    U32 ActiveSlots;
    U32 EventDword0 = 0;
    U32 EventDword1 = 0;
    U32 EventDword2 = 0;
    U32 EventDword3 = 0;
    U32 EventCycle = 0;
    U32 ExpectedCycle = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;

    if (Device == NULL) {
        return;
    }

    if (EnableSlotTimeoutLimiter.Initialized == FALSE && EnableSlotTimeoutLimiterInitAttempted == FALSE) {
        EnableSlotTimeoutLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&EnableSlotTimeoutLimiter,
                            XHCI_ENABLE_SLOT_TIMEOUT_LOG_IMMEDIATE_BUDGET,
                            XHCI_ENABLE_SLOT_TIMEOUT_LOG_INTERVAL_MS) == FALSE) {
            return;
        }
    }

    if (!RateLimiterShouldTrigger(&EnableSlotTimeoutLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    XHCI_LogHseTransitionIfNeeded(Device, TEXT("EnableSlotTimeout"));
    InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    UsbCommand = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    UsbStatus = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
    CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
    Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
    ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
    ActiveSlots = XHCI_CountActiveSlots(Device);
    PciCommand = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    PciStatus = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_STATUS);

    if (Device->EventRingLinear != 0) {
        LPXHCI_TRB EventRing = (LPXHCI_TRB)Device->EventRingLinear;
        U32 EventIndex = Device->EventRingDequeueIndex;
        XHCI_TRB Event = EventRing[EventIndex];
        EventDword0 = Event.Dword0;
        EventDword1 = Event.Dword1;
        EventDword2 = Event.Dword2;
        EventDword3 = Event.Dword3;
        EventCycle = (Event.Dword3 & XHCI_TRB_CYCLE) ? 1U : 0U;
        ExpectedCycle = Device->EventRingCycleState ? 1U : 0U;
    }

    WARNING(TEXT("[XHCI_LogEnableSlotTimeoutState] USBCMD=%x USBSTS=%x PCICMD=%x PCISTS=%x CRCR=%x:%x IMAN=%x ERDP=%x:%x Slots=%u/%u CQ=%u Event=%x:%x:%x:%x Cy=%u/%u suppressed=%u"),
            UsbCommand,
            UsbStatus,
            (U32)PciCommand,
            (U32)PciStatus,
            CrcrHigh,
            CrcrLow,
            Iman,
            ErdpHigh,
            ErdpLow,
            ActiveSlots,
            (U32)Device->MaxSlots,
            Device->CompletionCount,
            EventDword3,
            EventDword2,
            EventDword1,
            EventDword0,
            EventCycle,
            ExpectedCycle,
            Suppressed);
}

/************************************************************************/

/**
 * @brief Read current root port status for one USB device.
 * @param Device xHCI controller.
 * @param UsbDevice USB device state.
 * @return PORTSC raw value, 0 when unavailable.
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
 * @brief Return TRUE when one endpoint context is already configured.
 * @param Device xHCI device.
 * @param UsbDevice USB device.
 * @param Dci Endpoint DCI.
 * @return TRUE when endpoint state is not disabled.
 */
static BOOL XHCI_IsEndpointConfigured(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    LPXHCI_CONTEXT_32 EndpointContext;
    U32 EndpointState;

    if (Device == NULL || UsbDevice == NULL || UsbDevice->DeviceContextLinear == 0 || Dci == 0) {
        return FALSE;
    }

    // Device Context layout has slot context at index 0, then endpoint contexts at DCI index.
    EndpointContext = XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)Dci);
    EndpointState = EndpointContext->Dword0 & 0x7U;
    return (EndpointState != 0U) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Update slot Context Entries with the maximum between current and requested DCI.
 * @param SlotContext Slot context in the input context.
 * @param Dci Endpoint DCI to cover.
 */
static void XHCI_SetSlotContextEntriesForDci(LPXHCI_CONTEXT_32 SlotContext, U8 Dci) {
    U32 CurrentEntries;
    U32 TargetEntries;

    if (SlotContext == NULL || Dci == 0) {
        return;
    }

    CurrentEntries = (SlotContext->Dword0 >> XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT) & 0x1FU;
    TargetEntries = (U32)Dci;
    if (TargetEntries < CurrentEntries) {
        TargetEntries = CurrentEntries;
    }
    if (TargetEntries == 0) {
        TargetEntries = 1;
    }

    SlotContext->Dword0 &= ~(0x1FU << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    SlotContext->Dword0 |= ((TargetEntries & 0x1FU) << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
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
BOOL XHCI_InitTransferRingCore(LPCSTR Tag, PHYSICAL* PhysicalOut, LINEAR* LinearOut,
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

/**
 * @brief Populate an input context for Address Device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
void XHCI_BuildInputContextForAddress(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
void XHCI_BuildInputContextForEp0(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
BOOL XHCI_EnableSlot(LPXHCI_DEVICE Device, U8* SlotIdOut, U32* CompletionOut) {
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
        if (CompletionOut != NULL) {
            *CompletionOut = XHCI_ENUM_COMPLETION_TIMEOUT;
        }
        XHCI_LogEnableSlotTimeoutState(Device);
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        if (CompletionOut != NULL) {
            *CompletionOut = Completion;
        }
        ERROR(TEXT("[XHCI_EnableSlot] Completion code %u"), Completion);
        return FALSE;
    }

    if (SlotIdOut != NULL) {
        *SlotIdOut = SlotId;
    }
    if (CompletionOut != NULL) {
        *CompletionOut = Completion;
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
BOOL XHCI_AddressDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
BOOL XHCI_EvaluateContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
        WARNING(TEXT("[XHCI_ConfigureEndpoint] Timeout Slot=%x USBCMD=%x USBSTS=%x"),
                (U32)UsbDevice->SlotId,
                XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD),
                XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS));
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
        XHCI_SetSlotContextEntriesForDci(Slot, Endpoint->Dci);
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
    if (XHCI_IsEndpointConfigured(Device, UsbDevice, Endpoint->Dci)) {
        return TRUE;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << Endpoint->Dci);

    LPVOID SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    LPVOID SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);

    {
        LPXHCI_CONTEXT_32 Slot = (LPXHCI_CONTEXT_32)SlotOut;
        XHCI_SetSlotContextEntriesForDci(Slot, Endpoint->Dci);
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

    if (!XHCI_ConfigureEndpoint(Device, UsbDevice)) {
        if (XHCI_IsEndpointConfigured(Device, UsbDevice, Endpoint->Dci)) {
            return TRUE;
        }
        WARNING(TEXT("[XHCI_AddBulkEndpoint] Configure failed Slot=%x DCI=%x EP=%x MPS=%u"),
                (U32)UsbDevice->SlotId,
                (U32)Endpoint->Dci,
                (U32)Endpoint->Address,
                (U32)Endpoint->MaxPacketSize);
        return FALSE;
    }

    return TRUE;
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
