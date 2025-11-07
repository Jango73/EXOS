/************************************************************************\

    EXOS Kernel

    Generic device interrupt management

\************************************************************************/

#include "DeviceInterrupt.h"

#include "InterruptController.h"
#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "system/DeferredWork.h"

/***************************************************************************/

typedef struct tag_DEVICE_INTERRUPT_SLOT {
    BOOL InUse;
    LPDEVICE Device;
    U32 DeviceTypeID;
    U8 LegacyIRQ;
    U8 TargetCPU;
    DEVICE_INTERRUPT_ISR InterruptHandler;
    DEVICE_INTERRUPT_BOTTOM_HALF DeferredCallback;
    DEVICE_INTERRUPT_POLL PollCallback;
    LPVOID Context;
    U32 DeferredHandle;
    BOOL InterruptEnabled;
    STR Name[32];
} DEVICE_INTERRUPT_SLOT, *LPDEVICE_INTERRUPT_SLOT;

/***************************************************************************/

static DEVICE_INTERRUPT_SLOT g_DeviceSlots[DEVICE_INTERRUPT_VECTOR_COUNT];

/***************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context);
static void DeviceInterruptPollThunk(LPVOID Context);

/***************************************************************************/

void InitializeDeviceInterrupts(void) {
    MemorySet(g_DeviceSlots, 0, sizeof(g_DeviceSlots));
    DEBUG(TEXT("[InitializeDeviceInterrupts] Device interrupt slots cleared"));
}

/***************************************************************************/

BOOL DeviceInterruptRegister(const DEVICE_INTERRUPT_REGISTRATION *Registration, U8 *AssignedSlot) {
    if (Registration == NULL || Registration->Device == NULL || Registration->InterruptHandler == NULL) {
        ERROR(TEXT("[DeviceInterruptRegister] Invalid registration parameters"));
        return FALSE;
    }

    for (U32 Index = 0; Index < DEVICE_INTERRUPT_VECTOR_COUNT; Index++) {
        LPDEVICE_INTERRUPT_SLOT Slot = &g_DeviceSlots[Index];

        if (Slot->InUse) {
            continue;
        }

        MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
        Slot->InUse = TRUE;
        Slot->Device = Registration->Device;
        Slot->DeviceTypeID = ((LPLISTNODE)Registration->Device)->TypeID;
        Slot->LegacyIRQ = Registration->LegacyIRQ;
        Slot->TargetCPU = Registration->TargetCPU;
        Slot->InterruptHandler = Registration->InterruptHandler;
        Slot->DeferredCallback = Registration->DeferredCallback;
        Slot->PollCallback = Registration->PollCallback;
        Slot->Context = Registration->Context;
        MemorySet(Slot->Name, 0, sizeof(Slot->Name));
        if (Registration->Name != NULL) {
            StringCopyLimit(Slot->Name, Registration->Name, sizeof(Slot->Name));
        }
        Slot->InterruptEnabled = FALSE;

        DEFERRED_WORK_REGISTRATION WorkReg = {
            .WorkCallback = DeviceInterruptDeferredThunk,
            .PollCallback = DeviceInterruptPollThunk,
            .Context = (LPVOID)Slot,
            .Name = Slot->Name,
        };

        Slot->DeferredHandle = DeferredWorkRegister(&WorkReg);
        if (Slot->DeferredHandle == DEFERRED_WORK_INVALID_HANDLE) {
            ERROR(TEXT("[DeviceInterruptRegister] Failed to register deferred work for slot %u"), Index);
            MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
            return FALSE;
        }

        BOOL HasLegacyIRQ = (Registration->LegacyIRQ != 0xFFU);
        BOOL InterruptConfigured = FALSE;

        if (HasLegacyIRQ) {
            const U8 Vector = GetDeviceInterruptVector((U8)Index);

            if (ConfigureDeviceInterrupt(Registration->LegacyIRQ, Vector, Registration->TargetCPU)) {
                if (EnableDeviceInterrupt(Registration->LegacyIRQ)) {
                    InterruptConfigured = TRUE;
                } else {
                    WARNING(TEXT("[DeviceInterruptRegister] Failed to enable IRQ %u"), Registration->LegacyIRQ);
                }
            } else {
                WARNING(TEXT("[DeviceInterruptRegister] Failed to configure IRQ %u for vector %u"),
                        Registration->LegacyIRQ,
                        Vector);
            }
        }

        Slot->InterruptEnabled = InterruptConfigured;

        DEBUG(TEXT("[DeviceInterruptRegister] Slot %u assigned to device %p IRQ %u vector %u"),
              Index,
              Registration->Device,
              Registration->LegacyIRQ,
              GetDeviceInterruptVector((U8)Index));

        if (!InterruptConfigured) {
            DEBUG(TEXT("[DeviceInterruptRegister] Slot %u operating in polling mode"), Index);
        }

        if (AssignedSlot != NULL) {
            *AssignedSlot = (U8)Index;
        }

        return TRUE;
    }

    ERROR(TEXT("[DeviceInterruptRegister] No free device interrupt slots"));
    return FALSE;
}

/***************************************************************************/

BOOL DeviceInterruptUnregister(U8 SlotIndex) {
    if (SlotIndex >= DEVICE_INTERRUPT_VECTOR_COUNT) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &g_DeviceSlots[SlotIndex];
    if (!Slot->InUse) {
        return FALSE;
    }

    if (Slot->InterruptEnabled) {
        DisableDeviceInterrupt(Slot->LegacyIRQ);
    }
    DeferredWorkUnregister(Slot->DeferredHandle);

    DEBUG(TEXT("[DeviceInterruptUnregister] Slot %u released (IRQ %u)"), SlotIndex, Slot->LegacyIRQ);

    MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
    return TRUE;
}

/***************************************************************************/

void DeviceInterruptHandler(U8 SlotIndex) {
    if (SlotIndex >= DEVICE_INTERRUPT_VECTOR_COUNT) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &g_DeviceSlots[SlotIndex];
    if (!Slot->InUse) {
        static U32 SpuriousCount = 0;
        if (SpuriousCount < 4U) {
            DEBUG(TEXT("[DeviceInterruptHandler] Spurious device interrupt on slot %u"), SlotIndex);
        }
        SpuriousCount++;
        return;
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        BOOL ShouldSignal = TRUE;

        if (Slot->InterruptHandler != NULL) {
            ShouldSignal = Slot->InterruptHandler(Slot->Device, Slot->Context);
        }

        if (ShouldSignal) {
            DeferredWorkSignal(Slot->DeferredHandle);
        }
    }
}

/***************************************************************************/

BOOL DeviceInterruptSlotIsEnabled(U8 SlotIndex) {
    if (SlotIndex >= DEVICE_INTERRUPT_VECTOR_COUNT) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &g_DeviceSlots[SlotIndex];
    if (!Slot->InUse) {
        return FALSE;
    }

    return Slot->InterruptEnabled;
}

/***************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_SLOT Slot = (LPDEVICE_INTERRUPT_SLOT)Context;
    if (Slot == NULL || !Slot->InUse || Slot->DeferredCallback == NULL) {
        return;
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->DeferredCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/

static void DeviceInterruptPollThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_SLOT Slot = (LPDEVICE_INTERRUPT_SLOT)Context;
    if (Slot == NULL || !Slot->InUse || Slot->PollCallback == NULL) {
        return;
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->PollCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/
