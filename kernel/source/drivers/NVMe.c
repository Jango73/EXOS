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


    NVMe

\************************************************************************/

#include "drivers/NVMe.h"

#include "Driver.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/PCI.h"

/************************************************************************/

#define NVME_VER_MAJOR 1
#define NVME_VER_MINOR 0

#define NVME_ADMIN_QUEUE_ENTRIES 64u
#define NVME_ADMIN_SQ_ENTRY_SIZE 64u
#define NVME_ADMIN_CQ_ENTRY_SIZE 16u
#define NVME_ADMIN_QUEUE_ALIGNMENT N_4KB
#define NVME_READY_TIMEOUT_LOOPS 1000000u
#define NVME_IDENTIFY_TIMEOUT_LOOPS 1000000u

/************************************************************************/
// Forward declarations

static UINT NVMeCommands(UINT Function, UINT Param);
static UINT NVMeProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE NVMeAttach(LPPCI_DEVICE PciDevice);
static BOOL NVMeGetBar0Physical(LPPCI_DEVICE Device, PHYSICAL* BaseOut, U32* SizeOut);
static BOOL NVMeWaitForReady(LPNVME_DEVICE Device, BOOL Ready);
static BOOL NVMeSetupAdminQueues(LPNVME_DEVICE Device);
static void NVMeFreeAdminQueues(LPNVME_DEVICE Device);
static BOOL NVMeIdentifyController(LPNVME_DEVICE Device);
static BOOL NVMeIdentifyNamespace(LPNVME_DEVICE Device, U32 NamespaceId);

/************************************************************************/
// PCI match table and driver

static DRIVER_MATCH NVMeMatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, NVME_PCI_CLASS, NVME_PCI_SUBCLASS, NVME_PCI_PROG_IF}
};

PCI_DRIVER DATA_SECTION NVMePCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_STORAGE,
    .VersionMajor = NVME_VER_MAJOR,
    .VersionMinor = NVME_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "NVMe",
    .Product = "NVMe Controller",
    .Command = NVMeCommands,
    .Matches = NVMeMatchTable,
    .MatchCount = sizeof(NVMeMatchTable) / sizeof(NVMeMatchTable[0]),
    .Attach = NVMeAttach
};

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT NVMeCommands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return DF_RETURN_SUCCESS;
        case DF_UNLOAD:
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(NVME_VER_MAJOR, NVME_VER_MINOR);
        case DF_GET_LAST_FUNCTION:
            return DF_PROBE;
        case DF_PROBE:
            return NVMeProbe(Function, Param);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief PCI probe entry for the NVMe driver.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return DF_RETURN_SUCCESS on handled, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static UINT NVMeProbe(UINT Function, UINT Parameter) {
    LPPCI_INFO PciInfo = (LPPCI_INFO)Parameter;

    if (Function != DF_PROBE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Retrieve BAR0 physical address and size.
 *
 * @param Device PCI device pointer.
 * @param BaseOut Output physical base.
 * @param SizeOut Output size.
 * @return TRUE when BAR0 is valid.
 */
static BOOL NVMeGetBar0Physical(LPPCI_DEVICE Device, PHYSICAL* BaseOut, U32* SizeOut) {
    if (Device == NULL || BaseOut == NULL || SizeOut == NULL) {
        return FALSE;
    }

    U32 Bar0Raw = PCI_Read32(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_BAR0);
    if (PCI_BAR_IS_IO(Bar0Raw)) {
        return FALSE;
    }

    U32 Bar0Low = (Bar0Raw & PCI_BAR_MEM_MASK);
    U32 BarType = (Bar0Raw >> 1) & 0x3U;

    if (BarType == 0x2U) {
        U32 Bar0High = PCI_Read32(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_BAR1);
        if (Bar0High != 0) {
            return FALSE;
        }
    }

    *BaseOut = (PHYSICAL)Bar0Low;
    *SizeOut = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);

    return (*BaseOut != 0 && *SizeOut != 0);
}

/************************************************************************/

/**
 * @brief Wait for the controller ready state.
 *
 * @param Device NVMe device.
 * @param Ready TRUE to wait for ready, FALSE to wait for not ready.
 * @return TRUE on expected state, FALSE on timeout.
 */
static BOOL NVMeWaitForReady(LPNVME_DEVICE Device, BOOL Ready) {
    if (Device == NULL || Device->MmioBase == 0) {
        return FALSE;
    }

    volatile U32* Regs = (volatile U32*)Device->MmioBase;
    for (UINT Loop = 0; Loop < NVME_READY_TIMEOUT_LOOPS; Loop++) {
        U32 Csts = Regs[NVME_REG_CSTS / 4];
        BOOL IsReady = ((Csts & 0x1u) != 0u);
        if (IsReady == Ready) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Free admin queue memory.
 *
 * @param Device NVMe device.
 */
static void NVMeFreeAdminQueues(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    if (Device->AdminQueueRaw != NULL) {
        KernelHeapFree(Device->AdminQueueRaw);
    }

    Device->AdminQueueRaw = NULL;
    Device->AdminQueueBase = 0;
    Device->AdminQueuePhysical = 0;
    Device->AdminQueueSize = 0;
    Device->AdminSqEntries = 0;
    Device->AdminCqEntries = 0;
    Device->AdminSq = NULL;
    Device->AdminCq = NULL;
}

/************************************************************************/

/**
 * @brief Allocate and configure admin queues.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeSetupAdminQueues(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    Device->AdminSqEntries = NVME_ADMIN_QUEUE_ENTRIES;
    Device->AdminCqEntries = NVME_ADMIN_QUEUE_ENTRIES;

    U32 AdminSqSize = Device->AdminSqEntries * NVME_ADMIN_SQ_ENTRY_SIZE;
    U32 AdminCqSize = Device->AdminCqEntries * NVME_ADMIN_CQ_ENTRY_SIZE;
    Device->AdminQueueSize = AdminSqSize + AdminCqSize;

    U32 RawSize = Device->AdminQueueSize + NVME_ADMIN_QUEUE_ALIGNMENT;
    Device->AdminQueueRaw = KernelHeapAlloc(RawSize);
    if (Device->AdminQueueRaw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Device->AdminQueueRaw;
    LINEAR AlignedBase = (LINEAR)((RawBase + (NVME_ADMIN_QUEUE_ALIGNMENT - 1u)) &
        ~(NVME_ADMIN_QUEUE_ALIGNMENT - 1u));
    Device->AdminQueueBase = AlignedBase;
    MemorySet((LPVOID)Device->AdminQueueBase, 0, Device->AdminQueueSize);

    PHYSICAL BasePhys = MapLinearToPhysical(Device->AdminQueueBase);
    if (BasePhys == 0) {
        NVMeFreeAdminQueues(Device);
        return FALSE;
    }

    for (UINT Offset = 0; Offset < Device->AdminQueueSize; Offset += N_4KB) {
        LINEAR Linear = Device->AdminQueueBase + (LINEAR)Offset;
        PHYSICAL Physical = MapLinearToPhysical(Linear);
        if (Physical != (BasePhys + (PHYSICAL)Offset)) {
            NVMeFreeAdminQueues(Device);
            return FALSE;
        }
    }

    Device->AdminQueuePhysical = BasePhys;
    Device->AdminSq = (U8*)Device->AdminQueueBase;
    Device->AdminCq = (U8*)(Device->AdminQueueBase + AdminSqSize);
    Device->AdminSqTail = 0;
    Device->AdminCqHead = 0;
    Device->AdminCqPhase = 1u;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute doorbell register base for SQ0/CQ0.
 *
 * @param Device NVMe device.
 * @return Pointer to doorbell base.
 */
static volatile U32* NVMeGetDoorbellBase(LPNVME_DEVICE Device) {
    if (Device == NULL || Device->MmioBase == 0) {
        return NULL;
    }

    return (volatile U32*)((U8*)Device->MmioBase + 0x1000);
}

/************************************************************************/

/**
 * @brief Submit an admin command and wait for completion.
 *
 * @param Device NVMe device.
 * @param Command Command to submit.
 * @param CompletionOut Completion entry output (optional).
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeSubmitAdminCommand(LPNVME_DEVICE Device, const NVME_COMMAND* Command, NVME_COMPLETION* CompletionOut) {
    if (Device == NULL || Command == NULL || Device->AdminSq == NULL || Device->AdminCq == NULL) {
        return FALSE;
    }

    UINT Tail = Device->AdminSqTail;
    LPNVME_COMMAND Sq = (LPNVME_COMMAND)Device->AdminSq;
    LPNVME_COMPLETION Cq = (LPNVME_COMPLETION)Device->AdminCq;

    MemoryCopy(&Sq[Tail], Command, sizeof(NVME_COMMAND));
    Device->AdminSqTail = (Tail + 1u) % Device->AdminSqEntries;

    volatile U32* Doorbell = NVMeGetDoorbellBase(Device);
    if (Doorbell == NULL) {
        return FALSE;
    }

    UINT DbStride = (UINT)(Device->DoorbellStride / 4u);
    Doorbell[0] = (U32)Device->AdminSqTail;

    UINT Head = Device->AdminCqHead;
    U8 Phase = Device->AdminCqPhase;

    for (UINT Loop = 0; Loop < NVME_IDENTIFY_TIMEOUT_LOOPS; Loop++) {
        LPNVME_COMPLETION Entry = &Cq[Head];
        U16 Status = Entry->Status;
        U8 EntryPhase = (U8)(Status & 0x1u);
        if (EntryPhase == Phase) {
            if (CompletionOut != NULL) {
                *CompletionOut = *Entry;
            }

            Head++;
            if (Head >= Device->AdminCqEntries) {
                Head = 0;
                Phase ^= 1u;
            }

            Device->AdminCqHead = Head;
            Device->AdminCqPhase = Phase;
            Doorbell[DbStride] = (U32)Head;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Prepare an aligned buffer for identify data.
 *
 * @param BufferOut Receives aligned buffer pointer.
 * @param RawOut Receives raw allocation pointer.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeAllocateIdentifyBuffer(LPVOID* BufferOut, LPVOID* RawOut) {
    if (BufferOut == NULL || RawOut == NULL) {
        return FALSE;
    }

    *BufferOut = NULL;
    *RawOut = NULL;

    UINT RawSize = N_4KB + N_4KB;
    LPVOID Raw = KernelHeapAlloc(RawSize);
    if (Raw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Raw;
    LINEAR AlignedBase = (LINEAR)((RawBase + (N_4KB - 1u)) & ~(N_4KB - 1u));
    *BufferOut = (LPVOID)AlignedBase;
    *RawOut = Raw;
    MemorySet((LPVOID)AlignedBase, 0, N_4KB);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Trim trailing spaces from a fixed-size string.
 *
 * @param Text Buffer to trim.
 * @param MaxLength Buffer length.
 */
static void NVMeTrimString(STR* Text, UINT MaxLength) {
    if (Text == NULL || MaxLength == 0) {
        return;
    }

    INT Index = (INT)MaxLength - 1;
    while (Index >= 0) {
        if (Text[Index] != ' ') {
            Text[Index + 1] = STR_NULL;
            return;
        }
        Index--;
    }
    Text[0] = STR_NULL;
}

/************************************************************************/

/**
 * @brief Identify the NVMe controller.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeIdentifyController(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    LPVOID Buffer = NULL;
    LPVOID Raw = NULL;
    if (!NVMeAllocateIdentifyBuffer(&Buffer, &Raw)) {
        return FALSE;
    }

    PHYSICAL BufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (BufferPhys == 0 || (BufferPhys & (N_4KB - 1u)) != 0) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_IDENTIFY;
    Command.CommandId = 1u;
    Command.NamespaceId = 0u;
    Command.Prp1Low = (U32)(BufferPhys & 0xFFFFFFFFu);
    Command.Prp1High = 0u;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BufferPhys >> 32) & 0xFFFFFFFFu);
#endif
    Command.CommandDword10 = 1u;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0u) {
        WARNING(TEXT("[NVMeIdentifyController] Completion status %x"), (U32)Status);
        KernelHeapFree(Raw);
        return FALSE;
    }

    U8* Data = (U8*)Buffer;
    STR Serial[21];
    STR Model[41];
    STR Firmware[9];

    for (UINT Index = 0; Index < 20; Index++) {
        Serial[Index] = (STR)Data[4 + Index];
    }
    Serial[20] = STR_NULL;
    for (UINT Index = 0; Index < 40; Index++) {
        Model[Index] = (STR)Data[24 + Index];
    }
    Model[40] = STR_NULL;
    for (UINT Index = 0; Index < 8; Index++) {
        Firmware[Index] = (STR)Data[64 + Index];
    }
    Firmware[8] = STR_NULL;

    NVMeTrimString(Serial, 20);
    NVMeTrimString(Model, 40);
    NVMeTrimString(Firmware, 8);

    DEBUG(TEXT("[NVMeIdentifyController] Serial=%s Model=%s Firmware=%s"),
          Serial,
          Model,
          Firmware);

    KernelHeapFree(Raw);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Identify a namespace.
 *
 * @param Device NVMe device.
 * @param NamespaceId Namespace identifier.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeIdentifyNamespace(LPNVME_DEVICE Device, U32 NamespaceId) {
    if (Device == NULL || NamespaceId == 0u) {
        return FALSE;
    }

    LPVOID Buffer = NULL;
    LPVOID Raw = NULL;
    if (!NVMeAllocateIdentifyBuffer(&Buffer, &Raw)) {
        return FALSE;
    }

    PHYSICAL BufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (BufferPhys == 0 || (BufferPhys & (N_4KB - 1u)) != 0) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_IDENTIFY;
    Command.CommandId = 2u;
    Command.NamespaceId = NamespaceId;
    Command.Prp1Low = (U32)(BufferPhys & 0xFFFFFFFFu);
    Command.Prp1High = 0u;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BufferPhys >> 32) & 0xFFFFFFFFu);
#endif
    Command.CommandDword10 = 0u;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0u) {
        WARNING(TEXT("[NVMeIdentifyNamespace] Completion status %x"), (U32)Status);
        KernelHeapFree(Raw);
        return FALSE;
    }

    U8* Data = (U8*)Buffer;
    U64 Nsze;
#ifdef __EXOS_32__
    Nsze.LO = (U32)Data[0] |
              ((U32)Data[1] << 8) |
              ((U32)Data[2] << 16) |
              ((U32)Data[3] << 24);
    Nsze.HI = (U32)Data[4] |
              ((U32)Data[5] << 8) |
              ((U32)Data[6] << 16) |
              ((U32)Data[7] << 24);
#else
    Nsze = (U64)Data[0] |
           ((U64)Data[1] << 8) |
           ((U64)Data[2] << 16) |
           ((U64)Data[3] << 24) |
           ((U64)Data[4] << 32) |
           ((U64)Data[5] << 40) |
           ((U64)Data[6] << 48) |
           ((U64)Data[7] << 56);
#endif

    DEBUG(TEXT("[NVMeIdentifyNamespace] NSID=%u NSZE=%x%08x"),
          (U32)NamespaceId,
#ifdef __EXOS_32__
          (U32)Nsze.HI,
          (U32)Nsze.LO
#else
          (U32)((Nsze >> 32) & 0xFFFFFFFFu),
          (U32)(Nsze & 0xFFFFFFFFu)
#endif
    );

    KernelHeapFree(Raw);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Attach detected NVMe controller and map registers.
 *
 * @param PciDevice Detected PCI device descriptor.
 * @return Heap-allocated PCI device pointer or NULL on failure.
 */
static LPPCI_DEVICE NVMeAttach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    LPNVME_DEVICE Device = (LPNVME_DEVICE)KernelHeapAlloc(sizeof(NVME_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(NVME_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->Next = NULL;
    Device->Prev = NULL;
    Device->References = 1;

    PHYSICAL Bar0Physical = 0;
    U32 Bar0Size = 0;
    if (!NVMeGetBar0Physical((LPPCI_DEVICE)Device, &Bar0Physical, &Bar0Size)) {
        ERROR(TEXT("[NVMeAttach] Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(Bar0Physical, Bar0Size);
    Device->MmioSize = Bar0Size;
    if (Device->MmioBase == 0) {
        ERROR(TEXT("[NVMeAttach] MapIOMemory failed for %p size %u"),
              (LPVOID)(LINEAR)Bar0Physical,
              (U32)Bar0Size);
        KernelHeapFree(Device);
        return NULL;
    }

    volatile U32* Regs = (volatile U32*)Device->MmioBase;
    U32 CapLow = Regs[NVME_REG_CAP / 4];
    U32 CapHigh = Regs[(NVME_REG_CAP / 4) + 1];
    U32 Dstrd = CapHigh & 0xFu;
    U32 Version = Regs[NVME_REG_VS / 4];
    U32 Cc = Regs[NVME_REG_CC / 4];
    U32 Csts = Regs[NVME_REG_CSTS / 4];
    U32 Aqa = Regs[NVME_REG_AQA / 4];
    U32 AsqLow = Regs[NVME_REG_ASQ / 4];
    U32 AsqHigh = Regs[(NVME_REG_ASQ / 4) + 1];
    U32 AcqLow = Regs[NVME_REG_ACQ / 4];
    U32 AcqHigh = Regs[(NVME_REG_ACQ / 4) + 1];

    DEBUG(TEXT("[NVMeAttach] BAR0=%p size=%u CAP=%x/%x VS=%x CC=%x CSTS=%x AQA=%x"),
          (LPVOID)(LINEAR)Bar0Physical,
          (U32)Bar0Size,
          CapLow,
          CapHigh,
          Version,
          Cc,
          Csts,
          Aqa);
    DEBUG(TEXT("[NVMeAttach] ASQ=%x/%x ACQ=%x/%x"),
          AsqLow,
          AsqHigh,
          AcqLow,
          AcqHigh);

    Device->DoorbellStride = (U32)(4u << Dstrd);

    if (!NVMeSetupAdminQueues(Device)) {
        ERROR(TEXT("[NVMeAttach] Failed to allocate admin queues"));
        UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        KernelHeapFree(Device);
        return NULL;
    }

    U32 CcValue = Regs[NVME_REG_CC / 4];
    if ((CcValue & NVME_CC_EN) != 0u) {
        Regs[NVME_REG_CC / 4] = (CcValue & ~NVME_CC_EN);
        if (!NVMeWaitForReady(Device, FALSE)) {
            ERROR(TEXT("[NVMeAttach] Controller did not stop"));
            NVMeFreeAdminQueues(Device);
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            KernelHeapFree(Device);
            return NULL;
        }
    }

    CcValue = (0u << NVME_CC_CSS_SHIFT) |
              (0u << NVME_CC_MPS_SHIFT) |
              (0u << NVME_CC_AMS_SHIFT) |
              (0u << NVME_CC_SHN_SHIFT) |
              (6u << NVME_CC_IOSQES_SHIFT) |
              (4u << NVME_CC_IOCQES_SHIFT);
    U32 AqaValue = ((Device->AdminCqEntries - 1u) << 16) | (Device->AdminSqEntries - 1u);
    Regs[NVME_REG_AQA / 4] = AqaValue;

    PHYSICAL AsqPhys = Device->AdminQueuePhysical;
    PHYSICAL AcqPhys = Device->AdminQueuePhysical + (PHYSICAL)(Device->AdminSqEntries * NVME_ADMIN_SQ_ENTRY_SIZE);

    U32 AsqLowNew = (U32)(AsqPhys & 0xFFFFFFFFu);
    U32 AsqHighNew = 0u;
    U32 AcqLowNew = (U32)(AcqPhys & 0xFFFFFFFFu);
    U32 AcqHighNew = 0u;
#ifdef __EXOS_64__
    AsqHighNew = (U32)((AsqPhys >> 32) & 0xFFFFFFFFu);
    AcqHighNew = (U32)((AcqPhys >> 32) & 0xFFFFFFFFu);
#endif

    Regs[NVME_REG_ASQ / 4] = AsqLowNew;
    Regs[(NVME_REG_ASQ / 4) + 1] = AsqHighNew;
    Regs[NVME_REG_ACQ / 4] = AcqLowNew;
    Regs[(NVME_REG_ACQ / 4) + 1] = AcqHighNew;

    Regs[NVME_REG_CC / 4] = CcValue;
    Regs[NVME_REG_CC / 4] = (CcValue | NVME_CC_EN);
    if (!NVMeWaitForReady(Device, TRUE)) {
        ERROR(TEXT("[NVMeAttach] Controller did not become ready"));
        NVMeFreeAdminQueues(Device);
        UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[NVMeAttach] Admin queues ready ASQ=%p ACQ=%p AQA=%x"),
          (LPVOID)(LINEAR)AsqPhys,
          (LPVOID)(LINEAR)AcqPhys,
          AqaValue);

    if (!NVMeIdentifyController(Device)) {
        WARNING(TEXT("[NVMeAttach] Identify controller failed"));
    }
    if (!NVMeIdentifyNamespace(Device, 1u)) {
        WARNING(TEXT("[NVMeAttach] Identify namespace 1 failed"));
    }

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/
