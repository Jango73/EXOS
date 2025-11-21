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
#include "utils/Helpers.h"
#include "User.h"

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

typedef struct tag_DEVICE_INTERRUPT_ENTRY {
    DEVICE_INTERRUPT_SLOT Slot;
    U32 InterruptCount;
    U32 DeferredCount;
    U32 PollCount;
    U32 SuppressedCount;
} DEVICE_INTERRUPT_ENTRY, *LPDEVICE_INTERRUPT_ENTRY;

/***************************************************************************/

static LPDEVICE_INTERRUPT_ENTRY g_DeviceInterruptEntries = NULL;
static U32 g_DeviceInterruptEntriesSize = 0;
static U8 g_DeviceInterruptSlotCount = DEVICE_INTERRUPT_VECTOR_DEFAULT;

/***************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context);
static void DeviceInterruptPollThunk(LPVOID Context);
static void DeviceInterruptApplyConfiguration(void);
static BOOL DeviceInterruptAllocateEntries(void);
static LPDEVICE_INTERRUPT_ENTRY DeviceInterruptGetEntry(U32 SlotIndex);

/***************************************************************************/

#define DEVICE_INTERRUPT_VER_MAJOR 1
#define DEVICE_INTERRUPT_VER_MINOR 0

static UINT DeviceInterruptDriverCommands(UINT Function, UINT Parameter);

DRIVER DeviceInterruptDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = DEVICE_INTERRUPT_VER_MAJOR,
    .VersionMinor = DEVICE_INTERRUPT_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "DeviceInterrupts",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = DeviceInterruptDriverCommands};

/***************************************************************************/

U8 DeviceInterruptGetSlotCount(void) {
    U8 SlotCount = g_DeviceInterruptSlotCount;

    if (SlotCount == 0) {
        SlotCount = 1;
    }

    if (SlotCount > DEVICE_INTERRUPT_VECTOR_MAX) {
        SlotCount = DEVICE_INTERRUPT_VECTOR_MAX;
    }

    return SlotCount;
}

/***************************************************************************/

static void DeviceInterruptApplyConfiguration(void) {
    g_DeviceInterruptSlotCount = DEVICE_INTERRUPT_VECTOR_DEFAULT;

    LPCSTR SlotCountValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEVICE_INTERRUPT_SLOTS));
    if (STRING_EMPTY(SlotCountValue) == FALSE) {
        U32 Requested = StringToU32(SlotCountValue);

        if (Requested == 0) {
            WARNING(TEXT("[DeviceInterruptApplyConfiguration] Requested slot count is zero, forcing minimum of 1"));
            Requested = 1;
        }

        if (Requested > DEVICE_INTERRUPT_VECTOR_MAX) {
            WARNING(TEXT("[DeviceInterruptApplyConfiguration] Requested slot count %u exceeds capacity %u"),
                    Requested,
                    DEVICE_INTERRUPT_VECTOR_MAX);
            Requested = DEVICE_INTERRUPT_VECTOR_MAX;
        }

        g_DeviceInterruptSlotCount = (U8)Requested;
    }

    if (g_DeviceInterruptSlotCount == 0) {
        g_DeviceInterruptSlotCount = 1;
    }

    DEBUG(TEXT("[DeviceInterruptApplyConfiguration] Active slots=%u (capacity=%u)"),
          g_DeviceInterruptSlotCount,
          DEVICE_INTERRUPT_VECTOR_MAX);
}

/***************************************************************************/

static BOOL DeviceInterruptAllocateEntries(void) {
    const U8 SlotCount = g_DeviceInterruptSlotCount;
    if (SlotCount == 0) {
        ERROR(TEXT("[DeviceInterruptAllocateEntries] Slot count is zero"));
        return FALSE;
    }

    if (g_DeviceInterruptEntries != NULL) {
        MemorySet(g_DeviceInterruptEntries, 0, g_DeviceInterruptEntriesSize);
        return TRUE;
    }

    U32 AllocationSize = (U32)SlotCount * (U32)sizeof(DEVICE_INTERRUPT_ENTRY);
    U32 PageMask = PAGE_SIZE - 1;
    AllocationSize = (AllocationSize + PageMask) & ~PageMask;

    LINEAR Buffer = AllocKernelRegion(0, AllocationSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Buffer == 0) {
        ERROR(TEXT("[DeviceInterruptAllocateEntries] AllocKernelRegion failed (size=%u)"), AllocationSize);
        return FALSE;
    }

    g_DeviceInterruptEntries = (LPDEVICE_INTERRUPT_ENTRY)Buffer;
    g_DeviceInterruptEntriesSize = AllocationSize;
    MemorySet(g_DeviceInterruptEntries, 0, g_DeviceInterruptEntriesSize);

    DEBUG(TEXT("[DeviceInterruptAllocateEntries] Allocated %u bytes for %u slots"),
          AllocationSize,
          SlotCount);

    return TRUE;
}

/***************************************************************************/

static LPDEVICE_INTERRUPT_ENTRY DeviceInterruptGetEntry(U32 SlotIndex) {
    if (g_DeviceInterruptEntries == NULL) {
        return NULL;
    }

    if (SlotIndex >= (U32)g_DeviceInterruptSlotCount) {
        return NULL;
    }

    return &g_DeviceInterruptEntries[SlotIndex];
}

/***************************************************************************/

void InitializeDeviceInterrupts(void) {
    DeviceInterruptApplyConfiguration();
    if (!DeviceInterruptAllocateEntries()) {
        ERROR(TEXT("[InitializeDeviceInterrupts] Failed to allocate slot storage"));
        return;
    }
    DEBUG(TEXT("[InitializeDeviceInterrupts] Device interrupt slots cleared"));
}

/***************************************************************************/

/**
 * @brief Driver command handler for device interrupt management.
 *
 * DF_LOAD initializes slot storage and configuration once; DF_UNLOAD only
 * clears readiness.
 */
static UINT DeviceInterruptDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((DeviceInterruptDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_ERROR_SUCCESS;
            }

            InitializeDeviceInterrupts();
            if (g_DeviceInterruptEntries != NULL) {
                DeviceInterruptDriver.Flags |= DRIVER_FLAG_READY;
                return DF_ERROR_SUCCESS;
            }

            return DF_ERROR_UNEXPECT;

        case DF_UNLOAD:
            if ((DeviceInterruptDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_ERROR_SUCCESS;
            }

            DeviceInterruptDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;

        case DF_GETVERSION:
            return MAKE_VERSION(DEVICE_INTERRUPT_VER_MAJOR, DEVICE_INTERRUPT_VER_MINOR);
    }

    return DF_ERROR_NOTIMPL;
}

BOOL DeviceInterruptRegister(const DEVICE_INTERRUPT_REGISTRATION *Registration, U8 *AssignedSlot) {
    if (Registration == NULL || Registration->Device == NULL || Registration->InterruptHandler == NULL) {
        ERROR(TEXT("[DeviceInterruptRegister] Invalid registration parameters"));
        return FALSE;
    }

    if (g_DeviceInterruptEntries == NULL) {
        ERROR(TEXT("[DeviceInterruptRegister] Slot storage not initialized"));
        return FALSE;
    }

    const U8 SlotCount = DeviceInterruptGetSlotCount();

    for (U32 Index = 0; Index < SlotCount; Index++) {
        LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(Index);
        if (Entry == NULL) {
            continue;
        }

        LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;

        if (Slot->InUse) {
            continue;
        }

        MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
        Entry->InterruptCount = 0;
        Entry->DeferredCount = 0;
        Entry->PollCount = 0;
        Entry->SuppressedCount = 0;
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
            .Context = (LPVOID)Entry,
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
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        return FALSE;
    }

    if (Slot->InterruptEnabled) {
        DisableDeviceInterrupt(Slot->LegacyIRQ);
    }
    DeferredWorkUnregister(Slot->DeferredHandle);

    DEBUG(TEXT("[DeviceInterruptUnregister] Slot %u released (IRQ %u)"), SlotIndex, Slot->LegacyIRQ);

    MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
    Entry->InterruptCount = 0;
    Entry->DeferredCount = 0;
    Entry->PollCount = 0;
    Entry->SuppressedCount = 0;
    return TRUE;
}

/***************************************************************************/

void DeviceInterruptHandler(U8 SlotIndex) {
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        static U32 SpuriousCount = 0;
        if (SpuriousCount < INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("[DeviceInterruptHandler] Spurious device interrupt on slot %u"), SlotIndex);
        }
        SpuriousCount++;
        return;
    }

    Entry->InterruptCount++;
    if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
        DEBUG(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u Device=%p Count=%u Enabled=%s"),
              SlotIndex,
              Slot->LegacyIRQ,
              Slot->Device,
              Entry->InterruptCount,
              Slot->InterruptEnabled ? TEXT("YES") : TEXT("NO"));
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        BOOL ShouldSignal = TRUE;

        if (Slot->InterruptHandler != NULL) {
            ShouldSignal = Slot->InterruptHandler(Slot->Device, Slot->Context);
        }

        if (!ShouldSignal) {
            if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
                DEBUG(TEXT("[DeviceInterruptHandler] Slot=%u top-half suppressed deferred execution"), SlotIndex);
            }

            if (Slot->InterruptEnabled && Slot->InterruptHandler != NULL) {
                Entry->SuppressedCount++;
                BOOL ShouldWarn = (Entry->InterruptCount <= 8);
                if (!ShouldWarn && (Entry->InterruptCount & 0xFF) == 0) {
                    ShouldWarn = TRUE;
                }

                if (ShouldWarn) {
                    WARNING(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u handler suppressed signal while IRQ still armed (count=%u)"),
                            SlotIndex,
                            Slot->LegacyIRQ,
                            Entry->InterruptCount);
                }

                if (Entry->SuppressedCount >= DEVICE_INTERRUPT_SPURIOUS_THRESHOLD &&
                    Slot->LegacyIRQ != 0xFF) {
                    WARNING(TEXT("[DeviceInterruptHandler] Slot=%u IRQ=%u disabled after %u suppressed signals"),
                            SlotIndex,
                            Slot->LegacyIRQ,
                            Entry->SuppressedCount);
                    DisableDeviceInterrupt(Slot->LegacyIRQ);
                    Slot->InterruptEnabled = FALSE;
                    Entry->SuppressedCount = 0;
                    if (Slot->PollCallback != NULL) {
                        WARNING(TEXT("[DeviceInterruptHandler] Slot=%u falling back to polling"), SlotIndex);
                    }
                }
            }
        } else {
            Entry->SuppressedCount = 0;
            if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
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
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        return FALSE;
    }

    return Slot->InterruptEnabled;
}

/***************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_ENTRY Entry = (LPDEVICE_INTERRUPT_ENTRY)Context;
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse || Slot->DeferredCallback == NULL) {
        return;
    }

    const U32 SlotIndex = (U32)(Entry - g_DeviceInterruptEntries);
    if (SlotIndex < (U32)DeviceInterruptGetSlotCount()) {
        Entry->DeferredCount++;
        if (Entry->DeferredCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("[DeviceInterruptDeferredThunk] Slot=%u Name=%s Count=%u"),
                  SlotIndex,
                  Slot->Name,
                  Entry->DeferredCount);
        }
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->DeferredCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/

static void DeviceInterruptPollThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_ENTRY Entry = (LPDEVICE_INTERRUPT_ENTRY)Context;
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse || Slot->PollCallback == NULL) {
        return;
    }

    const U32 SlotIndex = (U32)(Entry - g_DeviceInterruptEntries);
    if (SlotIndex < (U32)DeviceInterruptGetSlotCount()) {
        Entry->PollCount++;
        if (Entry->PollCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("[DeviceInterruptPollThunk] Slot=%u Name=%s Count=%u"),
                  SlotIndex,
                  Slot->Name,
                  Entry->PollCount);
        }
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->PollCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/
