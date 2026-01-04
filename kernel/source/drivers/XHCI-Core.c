
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

#include "drivers/USBKeyboard.h"
#include "drivers/XHCI-Internal.h"

/************************************************************************/
// MMIO access

/**
 * @brief Read a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @return Register value.
 */
U32 XHCI_Read32(LINEAR Base, U32 Offset) {
    return *(volatile U32 *)((U8 *)Base + Offset);
}

/************************************************************************/

/**
 * @brief Write a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
void XHCI_Write32(LINEAR Base, U32 Offset, U32 Value) {
    *(volatile U32 *)((U8 *)Base + Offset) = Value;
}

/************************************************************************/

/**
 * @brief Write a 64-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
void XHCI_Write64(LINEAR Base, U32 Offset, U64 Value) {
    XHCI_Write32(Base, Offset, U64_Low32(Value));
    XHCI_Write32(Base, (U32)(Offset + 4), U64_High32(Value));
}

/************************************************************************/

/**
 * @brief Get pointer to an xHCI context within a context array.
 * @param Base Base of the context array.
 * @param ContextSize Context size in bytes.
 * @param Index Context index.
 * @return Pointer to context.
 */
LPXHCI_CONTEXT_32 XHCI_GetContextPointer(LINEAR Base, U32 ContextSize, U32 Index) {
    return (LPXHCI_CONTEXT_32)((U8 *)Base + (ContextSize * Index));
}

/************************************************************************/

/**
 * @brief Extract xHCI TRB type from Dword3.
 * @param Dword3 TRB Dword3 value.
 * @return TRB type.
 */
static U32 XHCI_GetTrbType(U32 Dword3) {
    return (Dword3 >> XHCI_TRB_TYPE_SHIFT) & 0x3F;
}

/************************************************************************/

/**
 * @brief Extract xHCI completion code from Dword2.
 * @param Dword2 TRB Dword2 value.
 * @return Completion code.
 */
static U32 XHCI_GetCompletionCode(U32 Dword2) {
    return (Dword2 >> 24) & 0xFF;
}

/************************************************************************/

/**
 * @brief Ring an xHCI doorbell.
 * @param Device xHCI device.
 * @param DoorbellIndex Doorbell index (slot ID).
 * @param Target Target endpoint.
 */
void XHCI_RingDoorbell(LPXHCI_DEVICE Device, U32 DoorbellIndex, U32 Target) {
    U32 Value = Target & XHCI_DOORBELL_TARGET_MASK;
    XHCI_Write32(Device->DoorbellBase, DoorbellIndex * sizeof(U32), Value);
}

/************************************************************************/

/**
 * @brief Get base address for interrupter register set 0.
 * @param Device xHCI device.
 * @return Interrupter base address.
 */
static LINEAR XHCI_GetInterrupterBase(LPXHCI_DEVICE Device) {
    return Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
}

/************************************************************************/

/**
 * @brief Clear pending interrupt status.
 * @param Device xHCI device.
 */
static void XHCI_ClearInterruptPending(LPXHCI_DEVICE Device) {
    LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
    U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    Iman |= XHCI_IMAN_IP;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, Iman);
}

/************************************************************************/

/**
 * @brief Enable or disable interrupter delivery.
 * @param Device xHCI device.
 * @param Enabled TRUE to enable interrupts, FALSE to disable.
 */
static void XHCI_SetInterruptEnabled(LPXHCI_DEVICE Device, BOOL Enabled) {
    LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
    U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    Iman &= ~XHCI_IMAN_IE;
    if (Enabled) {
        Iman |= XHCI_IMAN_IE;
    }
    Iman |= XHCI_IMAN_IP;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, Iman);
}

/************************************************************************/

/**
 * @brief Top-half xHCI interrupt handler.
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (xHCI device).
 * @return TRUE if interrupt was handled.
 */
static BOOL XHCI_InterruptTopHalf(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)Context;
    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        if (Device->RuntimeBase == 0) {
            return FALSE;
        }

        LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
        U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
        if ((Iman & XHCI_IMAN_IP) == 0U) {
            return FALSE;
        }

        XHCI_ClearInterruptPending(Device);
        Device->InterruptCount++;
        if (Device->InterruptCount <= 4U) {
            DEBUG(TEXT("[XHCI_InterruptTopHalf] Pending interrupt acknowledged (count=%u)"),
                  Device->InterruptCount);
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Bottom-half xHCI interrupt handler.
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (xHCI device).
 */
static void XHCI_InterruptBottomHalf(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)Context;
    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        XHCI_PollCompletions(Device);
        USBKeyboardOnXhciInterrupt(Device);
    }
}

/************************************************************************/

/**
 * @brief Poll-mode interrupt handler.
 * @param DevicePointer Device pointer from polling context.
 * @param Context Driver context (xHCI device).
 */
static void XHCI_InterruptPoll(LPDEVICE DevicePointer, LPVOID Context) {
    (void)XHCI_InterruptTopHalf(DevicePointer, Context);
    XHCI_InterruptBottomHalf(DevicePointer, Context);
}

/************************************************************************/

/**
 * @brief Register xHCI interrupts via DeviceInterrupt infrastructure.
 * @param Device xHCI device.
 * @return TRUE on success.
 */
static BOOL XHCI_RegisterInterrupts(LPXHCI_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    if (Device->InterruptRegistered) {
        return TRUE;
    }

    if (Device->Info.IRQLine == 0xFFU) {
        WARNING(TEXT("[XHCI_RegisterInterrupts] Controller reports no legacy IRQ line"));
    }

    DEVICE_INTERRUPT_REGISTRATION Registration = {
        .Device = (LPDEVICE)Device,
        .LegacyIRQ = Device->Info.IRQLine,
        .TargetCPU = 0,
        .InterruptHandler = XHCI_InterruptTopHalf,
        .DeferredCallback = XHCI_InterruptBottomHalf,
        .PollCallback = XHCI_InterruptPoll,
        .Context = (LPVOID)Device,
        .Name = (Device->Driver != NULL) ? Device->Driver->Product : TEXT("xHCI"),
    };

    if (!DeviceInterruptRegister(&Registration, &Device->InterruptSlot)) {
        WARNING(TEXT("[XHCI_RegisterInterrupts] Failed to register interrupt slot for IRQ %u"),
                Device->Info.IRQLine);
        Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        return FALSE;
    }

    Device->InterruptRegistered = TRUE;
    Device->InterruptEnabled = DeviceInterruptSlotIsEnabled(Device->InterruptSlot);
    XHCI_SetInterruptEnabled(Device, Device->InterruptEnabled);

    DEBUG(TEXT("[XHCI_RegisterInterrupts] Slot %u registered for IRQ %u (mode=%s)"),
          Device->InterruptSlot,
          Device->Info.IRQLine,
          Device->InterruptEnabled ? TEXT("INTERRUPT") : TEXT("POLLING"));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Unregister xHCI interrupts.
 * @param Device xHCI device.
 */
static void XHCI_UnregisterInterrupts(LPXHCI_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    if (!Device->InterruptRegistered) {
        return;
    }

    XHCI_SetInterruptEnabled(Device, FALSE);

    if (Device->InterruptSlot != DEVICE_INTERRUPT_INVALID_SLOT) {
        DeviceInterruptUnregister(Device->InterruptSlot);
    }

    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->InterruptRegistered = FALSE;
    Device->InterruptEnabled = FALSE;
}

/************************************************************************/

/**
 * @brief Record an xHCI completion event in the device queue.
 * @param Device xHCI device.
 * @param Event Event TRB.
 */
static void XHCI_PushCompletion(LPXHCI_DEVICE Device, const XHCI_TRB* Event) {
    if (Device == NULL || Event == NULL) {
        return;
    }

    U8 Type = (U8)XHCI_GetTrbType(Event->Dword3);
    if (Type != XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT &&
        Type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
        return;
    }

    U64 Pointer = U64_Make(Event->Dword1, Event->Dword0);
    U32 Completion = XHCI_GetCompletionCode(Event->Dword2);
    U8 SlotId = (U8)((Event->Dword3 >> 24) & 0xFF);

    if (Device->CompletionCount >= XHCI_COMPLETION_QUEUE_MAX) {
        for (U32 Index = 1; Index < Device->CompletionCount; Index++) {
            Device->CompletionQueue[Index - 1] = Device->CompletionQueue[Index];
        }
        Device->CompletionCount = XHCI_COMPLETION_QUEUE_MAX - 1;
    }

    XHCI_COMPLETION* Entry = &Device->CompletionQueue[Device->CompletionCount++];
    Entry->TrbPhysical = Pointer;
    Entry->Completion = Completion;
    Entry->Type = Type;
    Entry->SlotId = SlotId;
}

/************************************************************************/

/**
 * @brief Try to pop a completion entry for a TRB.
 * @param Device xHCI device.
 * @param Type Expected completion type.
 * @param TrbPhysical TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE if a matching completion was found.
 */
BOOL XHCI_PopCompletion(LPXHCI_DEVICE Device, U8 Type, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut) {
    if (Device == NULL) {
        return FALSE;
    }

    for (U32 Index = 0; Index < Device->CompletionCount; Index++) {
        XHCI_COMPLETION* Entry = &Device->CompletionQueue[Index];
        if (Entry->Type != Type) {
            continue;
        }
        if (!U64_EQUAL(Entry->TrbPhysical, TrbPhysical)) {
            continue;
        }

        if (SlotIdOut != NULL) {
            *SlotIdOut = Entry->SlotId;
        }
        if (CompletionOut != NULL) {
            *CompletionOut = Entry->Completion;
        }

        for (U32 Shift = Index + 1; Shift < Device->CompletionCount; Shift++) {
            Device->CompletionQueue[Shift - 1] = Device->CompletionQueue[Shift];
        }
        Device->CompletionCount--;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB in a ring using xHCI link semantics.
 * @param RingLinear Ring base (linear).
 * @param RingPhysical Ring base (physical).
 * @param EnqueueIndex Ring enqueue index (in/out).
 * @param CycleState Ring cycle state (in/out).
 * @param RingTrbs Number of TRBs in ring (including link TRB).
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
BOOL XHCI_RingEnqueue(LINEAR RingLinear, PHYSICAL RingPhysical, U32* EnqueueIndex, U32* CycleState,
                      U32 RingTrbs, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (RingLinear == 0 || RingPhysical == 0 || EnqueueIndex == NULL || CycleState == NULL || Trb == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)RingLinear;
    U32 Index = *EnqueueIndex;
    U32 LinkIndex = RingTrbs - 1;

    if (Index >= LinkIndex) {
        Index = 0;
        *EnqueueIndex = 0;
    }

    XHCI_TRB Local = *Trb;
    Local.Dword3 |= (*CycleState ? XHCI_TRB_CYCLE : 0);

    Ring[Index] = Local;

    if (PhysicalOut != NULL) {
        *PhysicalOut = U64_FromUINT(RingPhysical + (Index * sizeof(XHCI_TRB)));
    }

    Index++;
    if (Index == LinkIndex) {
        Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                 (*CycleState ? XHCI_TRB_CYCLE : 0) |
                                 XHCI_TRB_TOGGLE_CYCLE;
        *CycleState ^= 1;
        Index = 0;
    }

    *EnqueueIndex = Index;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on the command ring.
 * @param Device xHCI device.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
BOOL XHCI_CommandRingEnqueue(LPXHCI_DEVICE Device, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (Device == NULL || Trb == NULL) {
        return FALSE;
    }
    return XHCI_RingEnqueue(Device->CommandRingLinear, Device->CommandRingPhysical,
                            &Device->CommandRingEnqueueIndex, &Device->CommandRingCycleState,
                            XHCI_COMMAND_RING_TRBS, Trb, PhysicalOut);
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on a transfer ring.
 * @param UsbDevice USB device state.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
BOOL XHCI_TransferRingEnqueue(LPXHCI_USB_DEVICE UsbDevice, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (UsbDevice == NULL || Trb == NULL) {
        return FALSE;
    }
    return XHCI_RingEnqueue(UsbDevice->TransferRingLinear, UsbDevice->TransferRingPhysical,
                            &UsbDevice->TransferRingEnqueueIndex, &UsbDevice->TransferRingCycleState,
                            XHCI_TRANSFER_RING_TRBS, Trb, PhysicalOut);
}

/************************************************************************/

/**
 * @brief Dequeue one event TRB if available.
 * @param Device xHCI device.
 * @param EventOut Receives the event TRB.
 * @return TRUE if an event was dequeued.
 */
BOOL XHCI_DequeueEvent(LPXHCI_DEVICE Device, XHCI_TRB* EventOut) {
    if (Device == NULL || EventOut == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->EventRingLinear;
    U32 Index = Device->EventRingDequeueIndex;
    XHCI_TRB Event = Ring[Index];

    if (((Event.Dword3 & XHCI_TRB_CYCLE) != 0) != (Device->EventRingCycleState != 0)) {
        return FALSE;
    }

    *EventOut = Event;

    Index++;
    if (Index >= XHCI_EVENT_RING_TRBS) {
        Index = 0;
        Device->EventRingCycleState ^= 1;
    }

    Device->EventRingDequeueIndex = Index;

    {
        LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
        U64 Erdp = U64_FromUINT(Device->EventRingPhysical + (Index * sizeof(XHCI_TRB)));
        XHCI_Write64(InterrupterBase, XHCI_ERDP, Erdp);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Drain the event ring and cache completion events.
 * @param Device xHCI device.
 */
void XHCI_PollCompletions(LPXHCI_DEVICE Device) {
    XHCI_TRB Event;
    while (XHCI_DequeueEvent(Device, &Event)) {
        XHCI_PushCompletion(Device, &Event);
    }
}

/************************************************************************/

/**
 * @brief Busy-wait for a register to match a value.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Mask Mask applied to register.
 * @param Value Expected value after masking.
 * @param Timeout Loop bound.
 * @return TRUE on success, FALSE on timeout.
 */
BOOL XHCI_WaitForRegister(LINEAR Base, U32 Offset, U32 Mask, U32 Value, U32 Timeout) {
    U32 Count = 0;
    while (Count < Timeout) {
        if ((XHCI_Read32(Base, Offset) & Mask) == Value) {
            return TRUE;
        }
        Count++;
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate and map a single physical page.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL XHCI_AllocPage(LPCSTR Tag, PHYSICAL *PhysicalOut, LINEAR *LinearOut) {
    if (PhysicalOut == NULL || LinearOut == NULL) {
        return FALSE;
    }

    PHYSICAL Physical = AllocPhysicalPage();
    if (Physical == 0) {
        return FALSE;
    }

    LINEAR Linear = AllocKernelRegion(Physical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, Tag);
    if (Linear == 0) {
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Free xHCI allocations and MMIO mapping.
 * @param Device xHCI device.
 */
void XHCI_FreeResources(LPXHCI_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        XHCI_UnregisterInterrupts(Device);
        if (Device->HubPollHandle != DEFERRED_WORK_INVALID_HANDLE) {
            DeferredWorkUnregister(Device->HubPollHandle);
            Device->HubPollHandle = DEFERRED_WORK_INVALID_HANDLE;
        }

        if (Device->UsbDevices != NULL) {
            for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
                if (UsbDevice != NULL) {
                    XHCI_DestroyUsbDevice(Device, UsbDevice, TRUE);
                    Device->UsbDevices[PortIndex] = NULL;
                }
            }
            KernelHeapFree(Device->UsbDevices);
            Device->UsbDevices = NULL;
        }
        if (Device->EventRingTableLinear) {
            FreeRegion(Device->EventRingTableLinear, PAGE_SIZE);
            Device->EventRingTableLinear = 0;
        }
        if (Device->EventRingTablePhysical) {
            FreePhysicalPage(Device->EventRingTablePhysical);
            Device->EventRingTablePhysical = 0;
        }
        if (Device->EventRingLinear) {
            FreeRegion(Device->EventRingLinear, PAGE_SIZE);
            Device->EventRingLinear = 0;
        }
        if (Device->EventRingPhysical) {
            FreePhysicalPage(Device->EventRingPhysical);
            Device->EventRingPhysical = 0;
        }
        if (Device->CommandRingLinear) {
            FreeRegion(Device->CommandRingLinear, PAGE_SIZE);
            Device->CommandRingLinear = 0;
        }
        if (Device->CommandRingPhysical) {
            FreePhysicalPage(Device->CommandRingPhysical);
            Device->CommandRingPhysical = 0;
        }
        if (Device->DcbaaLinear) {
            FreeRegion(Device->DcbaaLinear, PAGE_SIZE);
            Device->DcbaaLinear = 0;
        }
        if (Device->DcbaaPhysical) {
            FreePhysicalPage(Device->DcbaaPhysical);
            Device->DcbaaPhysical = 0;
        }
        if (Device->MmioBase != 0 && Device->MmioSize != 0) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            Device->MmioBase = 0;
            Device->MmioSize = 0;
        }
    }
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
/**
 * @brief Read a port status register.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return PORTSC value.
 */
U32 XHCI_ReadPortStatus(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    return XHCI_Read32(Device->OpBase, Offset);
}

/************************************************************************/

/**
 * @brief Power on a port if supported by the controller.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 */
static void XHCI_PowerPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_PP) != 0) {
        return;
    }

    U32 WriteValue = PortStatus | XHCI_PORTSC_PP;
    WriteValue &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, WriteValue);
}

/************************************************************************/

/**
 * @brief Log port status to kernel log.
 * @param Device xHCI device.
 */
static void XHCI_LogPorts(LPXHCI_DEVICE Device) {
    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        BOOL Enabled = (PortStatus & XHCI_PORTSC_PED) != 0;

        DEBUG(TEXT("[XHCI_LogPorts] Port %u CCS=%u PED=%u Speed=%s Raw=%x"),
              PortIndex + 1,
              Connected ? 1U : 0U,
              Enabled ? 1U : 0U,
              XHCI_SpeedToString(SpeedId),
              PortStatus);
    }
}

/************************************************************************/

/**
 * @brief Initialize the command ring.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitCommandRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_CommandRing"), &Device->CommandRingPhysical, &Device->CommandRingLinear)) {
        ERROR(TEXT("[XHCI_InitCommandRing] Command ring allocation failed"));
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->CommandRingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_COMMAND_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(Device->CommandRingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    Device->CommandRingCycleState = 1;
    Device->CommandRingEnqueueIndex = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the event ring and interrupter 0.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitEventRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_EventRing"), &Device->EventRingPhysical, &Device->EventRingLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] Event ring allocation failed"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_EventRingTable"), &Device->EventRingTablePhysical, &Device->EventRingTableLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] ERST allocation failed"));
        return FALSE;
    }

    LPXHCI_ERST_ENTRY Entries = (LPXHCI_ERST_ENTRY)Device->EventRingTableLinear;
    MemorySet(Entries, 0, PAGE_SIZE);
    Entries[0].SegmentBase = U64_FromUINT(Device->EventRingPhysical);
    Entries[0].SegmentSize = XHCI_EVENT_RING_TRBS;
    Entries[0].Reserved = 0;
    Entries[0].Reserved2 = 0;

    LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, 0);
    XHCI_Write32(InterrupterBase, XHCI_IMOD, 0);
    XHCI_Write32(InterrupterBase, XHCI_ERSTSZ, 1);
    XHCI_Write64(InterrupterBase, XHCI_ERSTBA, U64_FromUINT(Device->EventRingTablePhysical));
    XHCI_Write64(InterrupterBase, XHCI_ERDP, U64_FromUINT(Device->EventRingPhysical));

    Device->EventRingDequeueIndex = 0;
    Device->EventRingCycleState = 1;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset and start the xHCI controller.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_ResetAndStart(LPXHCI_DEVICE Device) {
    U32 Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command &= ~XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, XHCI_HALT_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Halt timeout"));
        return FALSE;
    }

    Command |= XHCI_USBCMD_HCRST;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Reset bit timeout"));
        return FALSE;
    }

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Controller not ready"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DCBAA"), &Device->DcbaaPhysical, &Device->DcbaaLinear)) {
        ERROR(TEXT("[XHCI_ResetAndStart] DCBAA allocation failed"));
        return FALSE;
    }

    if (!XHCI_InitCommandRing(Device)) {
        return FALSE;
    }

    if (!XHCI_InitEventRing(Device)) {
        return FALSE;
    }

    XHCI_Write64(Device->OpBase, XHCI_OP_DCBAAP, U64_FromUINT(Device->DcbaaPhysical));

    {
        U64 Crcr = U64_FromUINT(Device->CommandRingPhysical);
        U32 Low = U64_Low32(Crcr) | XHCI_TRB_CYCLE;
        U32 High = U64_High32(Crcr);
        XHCI_Write32(Device->OpBase, XHCI_OP_CRCR, Low);
        XHCI_Write32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4), High);
    }

    XHCI_Write32(Device->OpBase, XHCI_OP_CONFIG, Device->MaxSlots);

    Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command |= XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0, XHCI_RUN_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Run timeout"));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize xHCI MMIO offsets and controller capabilities.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitController(LPXHCI_DEVICE Device) {
    U32 CapLengthReg = XHCI_Read32(Device->MmioBase, XHCI_CAPLENGTH);
    Device->CapLength = (U8)(CapLengthReg & MAX_U8);
    Device->HciVersion = (U16)((CapLengthReg >> 16) & 0xFFFF);

    U32 HcsParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCSPARAMS1);
    Device->MaxSlots = (U8)(HcsParams1 & XHCI_HCSPARAMS1_MAXSLOTS_MASK);
    Device->MaxInterrupters = (U16)((HcsParams1 & XHCI_HCSPARAMS1_MAXINTRS_MASK) >> XHCI_HCSPARAMS1_MAXINTRS_SHIFT);
    Device->MaxPorts = (U8)((HcsParams1 & XHCI_HCSPARAMS1_MAXPORTS_MASK) >> XHCI_HCSPARAMS1_MAXPORTS_SHIFT);
    Device->HccParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCCPARAMS1);
    Device->ContextSize = ((Device->HccParams1 & XHCI_HCCPARAMS1_CSZ) != 0) ? 64 : 32;

    Device->OpBase = Device->MmioBase + Device->CapLength;

    U32 DbOff = XHCI_Read32(Device->MmioBase, XHCI_DBOFF);
    U32 RtOff = XHCI_Read32(Device->MmioBase, XHCI_RTSOFF);
    Device->DoorbellBase = Device->MmioBase + (DbOff & 0xFFFFFFFC);
    Device->RuntimeBase = Device->MmioBase + (RtOff & 0xFFFFFFE0);

    DEBUG(TEXT("[XHCI_InitController] CapLen=%u HciVer=%x MaxSlots=%u MaxPorts=%u MaxIntrs=%u"),
          Device->CapLength,
          Device->HciVersion,
          Device->MaxSlots,
          Device->MaxPorts,
          Device->MaxInterrupters);

    U32 PageSize = XHCI_Read32(Device->OpBase, XHCI_OP_PAGESIZE);
    DEBUG(TEXT("[XHCI_InitController] PageSize bitmap=%x"), PageSize);

    if ((Device->HccParams1 & XHCI_HCCPARAMS1_AC64) == 0) {
        DEBUG(TEXT("[XHCI_InitController] 64-bit addressing not supported"));
    }

    if (!XHCI_ResetAndStart(Device)) {
        return FALSE;
    }

    if ((HcsParams1 & XHCI_HCSPARAMS1_PPC) != 0) {
        for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
            XHCI_PowerPort(Device, PortIndex);
        }
    }

    XHCI_LogPorts(Device);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Probe callback used by PCI subsystem.
 * @param PciInfo PCI device info.
 * @return DF_RETURN_SUCCESS when supported, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static U32 XHCI_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo == NULL) return DF_RETURN_BAD_PARAMETER;
    if (PciInfo->BaseClass != XHCI_CLASS_SERIAL_BUS) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->SubClass != XHCI_SUBCLASS_USB) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->ProgIF != XHCI_PROGIF_XHCI) return DF_RETURN_NOT_IMPLEMENTED;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Load callback for driver.
 * @return DF_RETURN_SUCCESS.
 */
static U32 XHCI_OnLoad(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Unload callback for driver.
 * @return DF_RETURN_SUCCESS.
 */
static U32 XHCI_OnUnload(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Version callback for driver.
 * @return Encoded version.
 */
static U32 XHCI_OnGetVersion(void) { return MAKE_VERSION(1, 0); }

/************************************************************************/

/**
 * @brief Capabilities callback for driver.
 * @return Capability bitmask.
 */
static U32 XHCI_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Last function callback.
 * @return Last function ID.
 */
static U32 XHCI_OnGetLastFunc(void) { return DF_PROBE; }

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT XHCI_Commands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return XHCI_OnLoad();
        case DF_UNLOAD:
            return XHCI_OnUnload();
        case DF_GET_VERSION:
            return XHCI_OnGetVersion();
        case DF_GET_CAPS:
            return XHCI_OnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return XHCI_OnGetLastFunc();
        case DF_PROBE:
            return XHCI_OnProbe((const PCI_INFO *)(LPVOID)Param);
        case DF_ENUM_NEXT:
            return XHCI_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Param);
        case DF_ENUM_PRETTY:
            return XHCI_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Param);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDevice PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE XHCI_Attach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    DEBUG(TEXT("[XHCI_Attach] New device %x:%x.%u"),
          (U32)PciDevice->Info.Bus,
          (U32)PciDevice->Info.Dev,
          (U32)PciDevice->Info.Func);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)KernelHeapAlloc(sizeof(XHCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(XHCI_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->HubPollHandle = DEFERRED_WORK_INVALID_HANDLE;

    U32 Bar0Raw = Device->Info.BAR[0];
    U32 Bar1Raw = Device->Info.BAR[1];
    U32 Bar0Base = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    BOOL Is64Bit = ((Bar0Raw & 0x6) == 0x4);

    if (Is64Bit && Bar1Raw != 0) {
        ERROR(TEXT("[XHCI_Attach] 64-bit BAR above 4GB not supported (BAR1=%x)"), Bar1Raw);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Bar0Base == 0 || Bar0Size == 0) {
        ERROR(TEXT("[XHCI_Attach] Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(Bar0Base, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == 0) {
        ERROR(TEXT("[XHCI_Attach] MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!XHCI_InitController(Device)) {
        ERROR(TEXT("[XHCI_Attach] Controller init failed"));
        XHCI_FreeResources(Device);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Device->MaxPorts > 0) {
        U32 PortCount = Device->MaxPorts;
        Device->UsbDevices = (LPXHCI_USB_DEVICE*)KernelHeapAlloc(sizeof(LPXHCI_USB_DEVICE) * PortCount);
        if (Device->UsbDevices == NULL) {
            ERROR(TEXT("[XHCI_Attach] USB device state allocation failed"));
            XHCI_FreeResources(Device);
            KernelHeapFree(Device);
            return NULL;
        }
        MemorySet(Device->UsbDevices, 0, sizeof(LPXHCI_USB_DEVICE) * PortCount);
        for (U32 PortIndex = 0; PortIndex < PortCount; PortIndex++) {
            LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)CreateKernelObject(sizeof(XHCI_USB_DEVICE),
                                                                                KOID_USBDEVICE);
            if (UsbDevice == NULL) {
                ERROR(TEXT("[XHCI_Attach] USB device object allocation failed"));
                XHCI_FreeResources(Device);
                KernelHeapFree(Device);
                return NULL;
            }
            XHCI_InitUsbDeviceObject(Device, UsbDevice);
            UsbDevice->IsRootPort = TRUE;
            UsbDevice->PortNumber = (U8)(PortIndex + 1);
            UsbDevice->RootPortNumber = UsbDevice->PortNumber;
            UsbDevice->Depth = 0;
            UsbDevice->RouteString = 0;
            Device->UsbDevices[PortIndex] = UsbDevice;
            XHCI_AddDeviceToList(Device, UsbDevice);
        }
    }

    (void)XHCI_RegisterInterrupts(Device);
    XHCI_RegisterHubPoll(Device);

    DEBUG(TEXT("[XHCI_Attach] Attached MMIO=%p Size=%u MaxPorts=%u"),
          Device->MmioBase,
          Device->MmioSize,
          Device->MaxPorts);

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

static DRIVER_MATCH XHCI_MatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, XHCI_CLASS_SERIAL_BUS, XHCI_SUBCLASS_USB, XHCI_PROGIF_XHCI}
};

PCI_DRIVER DATA_SECTION XHCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "xHCI",
    .Command = XHCI_Commands,
    .EnumDomainCount = 3,
    .EnumDomains = {ENUM_DOMAIN_XHCI_PORT, ENUM_DOMAIN_USB_DEVICE, ENUM_DOMAIN_USB_NODE},
    .Matches = XHCI_MatchTable,
    .MatchCount = sizeof(XHCI_MatchTable) / sizeof(XHCI_MatchTable[0]),
    .Attach = XHCI_Attach
};
