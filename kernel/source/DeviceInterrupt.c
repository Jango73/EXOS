/************************************************************************\

    EXOS Kernel

    Generic device interrupt management

\************************************************************************/

#include "DeviceInterrupt.h"

#include "InterruptController.h"
#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "DeferredWork.h"

/***************************************************************************/

#define DEVICE_INTERRUPT_SPURIOUS_THRESHOLD 64U

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
static U32 g_SlotInterruptCount[DEVICE_INTERRUPT_VECTOR_COUNT];
static U32 g_SlotDeferredCount[DEVICE_INTERRUPT_VECTOR_COUNT];
static U32 g_SlotPollCount[DEVICE_INTERRUPT_VECTOR_COUNT];
static U32 g_SlotSuppressedCount[DEVICE_INTERRUPT_VECTOR_COUNT];

/***************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context);
static void DeviceInterruptPollThunk(LPVOID Context);

/***************************************************************************/

void InitializeDeviceInterrupts(void) {
    MemorySet(g_DeviceSlots, 0, sizeof(g_DeviceSlots));
    MemorySet(g_SlotInterruptCount, 0, sizeof(g_SlotInterruptCount));
    MemorySet(g_SlotDeferredCount, 0, sizeof(g_SlotDeferredCount));
    MemorySet(g_SlotPollCount, 0, sizeof(g_SlotPollCount));
    MemorySet(g_SlotSuppressedCount, 0, sizeof(g_SlotSuppressedCount));
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
        g_SlotSuppressedCount[Index] = 0;
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
        BOOL PollingMode = DeferredWorkIsPollingMode();
        BOOL ShouldConfigureInterrupt = (HasLegacyIRQ && !PollingMode);

        if (ShouldConfigureInterrupt) {
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

        if (!ShouldConfigureInterrupt) {
            DEBUG(TEXT("[DeviceInterruptRegister] Slot %u operating in polling mode (IRQ setup skipped)"), Index);
        } else if (!InterruptConfigured) {
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
    g_SlotSuppressedCount[SlotIndex] = 0;
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

    g_SlotInterruptCount[SlotIndex]++;
    if (g_SlotInterruptCount[SlotIndex] <= 4U) {
        DEBUG(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u Device=%p Count=%u Enabled=%s"),
              SlotIndex,
              Slot->LegacyIRQ,
              Slot->Device,
              g_SlotInterruptCount[SlotIndex],
              Slot->InterruptEnabled ? TEXT("YES") : TEXT("NO"));
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        BOOL ShouldSignal = TRUE;

        if (Slot->InterruptHandler != NULL) {
            ShouldSignal = Slot->InterruptHandler(Slot->Device, Slot->Context);
        }

        if (!ShouldSignal) {
            if (g_SlotInterruptCount[SlotIndex] <= 4U) {
                DEBUG(TEXT("[DeviceInterruptHandler] Slot=%u top-half suppressed deferred execution"), SlotIndex);
            }

            if (Slot->InterruptEnabled && Slot->InterruptHandler != NULL) {
                g_SlotSuppressedCount[SlotIndex]++;
                BOOL ShouldWarn = (g_SlotInterruptCount[SlotIndex] <= 8U);
                if (!ShouldWarn && (g_SlotInterruptCount[SlotIndex] & 0xFFU) == 0U) {
                    ShouldWarn = TRUE;
                }

                if (ShouldWarn) {
                    WARNING(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u handler suppressed signal while IRQ still armed (count=%u)"),
                            SlotIndex,
                            Slot->LegacyIRQ,
                            g_SlotInterruptCount[SlotIndex]);
                }

                if (g_SlotSuppressedCount[SlotIndex] >= DEVICE_INTERRUPT_SPURIOUS_THRESHOLD &&
                    Slot->LegacyIRQ != 0xFFU) {
                    WARNING(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u disabled after %u suppressed signals"),
                            SlotIndex,
                            Slot->LegacyIRQ,
                            g_SlotSuppressedCount[SlotIndex]);
                    DisableDeviceInterrupt(Slot->LegacyIRQ);
                    Slot->InterruptEnabled = FALSE;
                    g_SlotSuppressedCount[SlotIndex] = 0;
                    if (Slot->PollCallback != NULL) {
                        WARNING(TEXT("[DeviceInterruptHandler] Slot=%u falling back to polling"), SlotIndex);
                    }
                }
            }
        } else {
            g_SlotSuppressedCount[SlotIndex] = 0;
            if (g_SlotInterruptCount[SlotIndex] <= 4U) {
                DEBUG(TEXT("[DeviceInterruptHandler] Slot=%u signaling deferred handle %u"),
                      SlotIndex,
                      Slot->DeferredHandle);
            }
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

    const U32 SlotIndex = (U32)(Slot - g_DeviceSlots);
    if (SlotIndex < DEVICE_INTERRUPT_VECTOR_COUNT) {
        g_SlotDeferredCount[SlotIndex]++;
        if (g_SlotDeferredCount[SlotIndex] <= 4U) {
            DEBUG(TEXT("[DeviceInterruptDeferredThunk] Slot=%u Name=%s Count=%u"),
                  SlotIndex,
                  Slot->Name,
                  g_SlotDeferredCount[SlotIndex]);
        }
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

    const U32 SlotIndex = (U32)(Slot - g_DeviceSlots);
    if (SlotIndex < DEVICE_INTERRUPT_VECTOR_COUNT) {
        g_SlotPollCount[SlotIndex]++;
        if (g_SlotPollCount[SlotIndex] <= 4U) {
            DEBUG(TEXT("[DeviceInterruptPollThunk] Slot=%u Name=%s Count=%u"),
                  SlotIndex,
                  Slot->Name,
                  g_SlotPollCount[SlotIndex]);
        }
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->PollCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/
