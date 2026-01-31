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


    NVMe (I/O)

\************************************************************************/

#include "drivers/NVMe-Internal.h"
#include "Disk.h"

/************************************************************************/

/**
 * @brief Free I/O queue memory.
 *
 * @param Device NVMe device.
 */
void NVMeFreeIoQueues(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    if (Device->IoQueueRaw != NULL) {
        KernelHeapFree(Device->IoQueueRaw);
    }

    Device->IoQueueRaw = NULL;
    Device->IoQueueBase = 0;
    Device->IoQueuePhysical = 0;
    Device->IoQueueSize = 0;
    Device->IoSqEntries = 0;
    Device->IoCqEntries = 0;
    Device->IoSq = NULL;
    Device->IoCq = NULL;
    Device->IoSqTail = 0;
    Device->IoCqHead = 0;
    Device->IoCqPhase = 0;
    Device->IoQueueId = 0;
    Device->IoCommandId = 0;
}

/************************************************************************/

/**
 * @brief Allocate and configure I/O queues.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeSetupIoQueues(LPNVME_DEVICE Device) {
    if (Device == NULL || Device->MmioBase == 0) {
        return FALSE;
    }

    volatile U32* Regs = (volatile U32*)Device->MmioBase;
    U32 CapLow = Regs[NVME_REG_CAP / 4];
    U32 MaxQueueEntries = (CapLow & 0xFFFF) + 1;

    Device->IoSqEntries = NVME_IO_QUEUE_ENTRIES;
    if (MaxQueueEntries != 0 && Device->IoSqEntries > MaxQueueEntries) {
        Device->IoSqEntries = MaxQueueEntries;
    }
    if (Device->IoSqEntries < 2) {
        return FALSE;
    }

    Device->IoCqEntries = Device->IoSqEntries;
    Device->IoQueueId = 1;

    U32 IoSqSize = Device->IoSqEntries * NVME_IO_SQ_ENTRY_SIZE;
    U32 IoCqSize = Device->IoCqEntries * NVME_IO_CQ_ENTRY_SIZE;
    U32 RawSize = IoSqSize + IoCqSize + (2 * NVME_IO_QUEUE_ALIGNMENT);
    Device->IoQueueRaw = KernelHeapAlloc(RawSize);
    if (Device->IoQueueRaw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Device->IoQueueRaw;
    LINEAR AlignedSqBase = (LINEAR)((RawBase + (NVME_IO_QUEUE_ALIGNMENT - 1)) &
        ~(NVME_IO_QUEUE_ALIGNMENT - 1));
    LINEAR AlignedCqBase = (LINEAR)((AlignedSqBase + IoSqSize + (NVME_IO_QUEUE_ALIGNMENT - 1)) &
        ~(NVME_IO_QUEUE_ALIGNMENT - 1));
    U32 TotalSpan = (U32)(AlignedCqBase - AlignedSqBase) + IoCqSize;

    Device->IoQueueBase = AlignedSqBase;
    Device->IoQueueSize = TotalSpan;
    MemorySet((LPVOID)Device->IoQueueBase, 0, Device->IoQueueSize);

    PHYSICAL BasePhys = MapLinearToPhysical(Device->IoQueueBase);
    if (BasePhys == 0) {
        NVMeFreeIoQueues(Device);
        return FALSE;
    }

    for (UINT Offset = 0; Offset < Device->IoQueueSize; Offset += N_4KB) {
        LINEAR Linear = Device->IoQueueBase + (LINEAR)Offset;
        PHYSICAL Physical = MapLinearToPhysical(Linear);
        if (Physical != (BasePhys + (PHYSICAL)Offset)) {
            NVMeFreeIoQueues(Device);
            return FALSE;
        }
    }

    Device->IoQueuePhysical = BasePhys;
    Device->IoSq = (U8*)Device->IoQueueBase;
    Device->IoCq = (U8*)AlignedCqBase;
    Device->IoSqTail = 0;
    Device->IoCqHead = 0;
    Device->IoCqPhase = 1;
    Device->IoCommandId = 1;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute doorbell register base for SQ0/CQ0.
 *
 * @param Device NVMe device.
 * @return Pointer to doorbell base.
 */
volatile U32* NVMeGetDoorbellBase(LPNVME_DEVICE Device) {
    if (Device == NULL || Device->MmioBase == 0) {
        return NULL;
    }

    return (volatile U32*)((U8*)Device->MmioBase + 0x1000);
}

/************************************************************************/

/**
 * @brief Drain I/O completion queue entries.
 *
 * @param Device NVMe device.
 * @return Number of completions processed.
 */
static UINT NVMeProcessIoCompletions(LPNVME_DEVICE Device) {
    if (Device == NULL || Device->IoCq == NULL) {
        return 0;
    }

    LPNVME_COMPLETION Cq = (LPNVME_COMPLETION)Device->IoCq;
    UINT Head = Device->IoCqHead;
    U8 Phase = Device->IoCqPhase;
    UINT Completed = 0;

    while (Completed < Device->IoCqEntries) {
        LPNVME_COMPLETION Entry = &Cq[Head];
        U16 Status = Entry->Status;
        U8 EntryPhase = (U8)(Status & 0x1);
        if (EntryPhase != Phase) {
            break;
        }

        Head++;
        if (Head >= Device->IoCqEntries) {
            Head = 0;
            Phase ^= 1;
        }
        Completed++;
    }

    if (Completed == 0) {
        return 0;
    }

    Device->IoCqHead = Head;
    Device->IoCqPhase = Phase;

    volatile U32* Doorbell = NVMeGetDoorbellBase(Device);
    if (Doorbell != NULL) {
        UINT DbStride = (UINT)(Device->DoorbellStride / 4);
        UINT QueueId = (UINT)Device->IoQueueId;
        UINT CqDoorbellIndex = ((QueueId * 2) + 1) * DbStride;
        Doorbell[CqDoorbellIndex] = (U32)Head;
    }

    return Completed;
}

/************************************************************************/

/**
 * @brief NVMe interrupt handler (top-half).
 *
 * @param Device Device pointer.
 * @param Context Optional context pointer.
 * @return TRUE to signal deferred work, FALSE to suppress.
 */
static BOOL NVMeInterruptHandler(LPDEVICE Device, LPVOID Context) {
    LPNVME_DEVICE NvmeDevice = (LPNVME_DEVICE)Device;
    UNUSED(Context);

    if (NvmeDevice == NULL || NvmeDevice->IoCq == NULL) {
        return FALSE;
    }

    UINT Completed = NVMeProcessIoCompletions(NvmeDevice);
    if (Completed == 0) {
        return FALSE;
    }

    DEBUG(TEXT("[NVMeInterruptHandler] Completed=%u"), Completed);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Enable MSI-X and program vector 0.
 *
 * @param Device NVMe device.
 * @param Vector Interrupt vector.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeEnableMsix(LPNVME_DEVICE Device, U8 Vector) {
    if (Device == NULL) {
        return FALSE;
    }

    U8 CapOffset = PCI_FindCapability(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CAP_ID_MSIX);
    if (CapOffset == 0) {
        WARNING(TEXT("[NVMeEnableMsix] MSI-X capability not found"));
        return FALSE;
    }

    U16 Control = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, (U16)(CapOffset + 2));
    U16 TableSize = (U16)((Control & 0x07FF) + 1);
    U32 TableInfo = PCI_Read32(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, (U16)(CapOffset + 4));
    U8 TableBir = (U8)(TableInfo & 0x7);
    U32 TableOffset = TableInfo & ~0x7;

    DEBUG(TEXT("[NVMeEnableMsix] Cap=%x Control=%x TableSize=%x BIR=%x Offset=%x"),
          (U32)CapOffset,
          (U32)Control,
          (U32)TableSize,
          (U32)TableBir,
          (U32)TableOffset);

    if (TableBir != 0) {
        WARNING(TEXT("[NVMeEnableMsix] Unsupported MSI-X table BIR %u"), (U32)TableBir);
        return FALSE;
    }

    if (Device->MmioBase == 0 || Device->MmioSize == 0) {
        WARNING(TEXT("[NVMeEnableMsix] Invalid BAR0 mapping"));
        return FALSE;
    }

    U32 TableBytes = (U32)TableSize * 16;
    U32 NeededSize = TableOffset + TableBytes;
    if (NeededSize > Device->MmioSize) {
        WARNING(TEXT("[NVMeEnableMsix] MSI-X table exceeds BAR0 size"));
        return FALSE;
    }

    volatile U32* Entry = (volatile U32*)(Device->MmioBase + (LINEAR)TableOffset);
    U32 ApicId = (U32)GetLocalAPICId();
    U32 AddressLow = 0xFEE00000 | (ApicId << 12);

    Entry[0] = AddressLow;
    Entry[1] = 0;
    Entry[2] = (U32)Vector;
    Entry[3] = 0;

    Control &= (U16)~0x4000;
    Control |= (U16)0x8000;
    PCI_Write16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, (U16)(CapOffset + 2), Control);

    DEBUG(TEXT("[NVMeEnableMsix] Enabled MSI-X vector %x"), (U32)Vector);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Register a device interrupt slot and enable MSI-X.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeSetupInterrupts(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    DEVICE_INTERRUPT_REGISTRATION Registration;
    MemorySet(&Registration, 0, sizeof(Registration));
    Registration.Device = (LPDEVICE)Device;
    Registration.LegacyIRQ = 0xFF;
    Registration.TargetCPU = 0;
    Registration.InterruptHandler = NVMeInterruptHandler;
    Registration.Context = Device;
    Registration.Name = TEXT("NVMe");

    if (!DeviceInterruptRegister(&Registration, &Device->InterruptSlot)) {
        WARNING(TEXT("[NVMeSetupInterrupts] Device interrupt registration failed"));
        Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        return FALSE;
    }

    Device->MsixVector = GetDeviceInterruptVector(Device->InterruptSlot);
    if (!NVMeEnableMsix(Device, Device->MsixVector)) {
        WARNING(TEXT("[NVMeSetupInterrupts] MSI-X setup failed"));
        DeviceInterruptUnregister(Device->InterruptSlot);
        Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        return FALSE;
    }

    Device->MsixEnabled = TRUE;

    if (Device->MmioBase != 0) {
        volatile U32* Regs = (volatile U32*)Device->MmioBase;
        Regs[NVME_REG_INTMC / 4] = 1 << 0;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Submit an I/O command and wait for completion.
 *
 * @param Device NVMe device.
 * @param Command I/O command to submit.
 * @param CompletionOut Completion output (optional).
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeSubmitIoCommand(LPNVME_DEVICE Device, const NVME_COMMAND* Command, NVME_COMPLETION* CompletionOut) {
    if (Device == NULL || Command == NULL || Device->IoSq == NULL || Device->IoCq == NULL) {
        return FALSE;
    }

    if (Device->IoCommandId == 0) {
        Device->IoCommandId = 1;
    }

    NVME_COMMAND LocalCommand = *Command;
    U16 CommandId = Device->IoCommandId;
    Device->IoCommandId = (U16)(Device->IoCommandId + 1);
    if (Device->IoCommandId == 0) {
        Device->IoCommandId = 1;
    }
    LocalCommand.CommandId = CommandId;

    UINT Tail = Device->IoSqTail;
    LPNVME_COMMAND Sq = (LPNVME_COMMAND)Device->IoSq;
    MemoryCopy(&Sq[Tail], &LocalCommand, sizeof(NVME_COMMAND));
    Device->IoSqTail = (Tail + 1) % Device->IoSqEntries;

    volatile U32* Doorbell = NVMeGetDoorbellBase(Device);
    if (Doorbell == NULL) {
        return FALSE;
    }

    UINT DbStride = (UINT)(Device->DoorbellStride / 4);
    UINT QueueId = (UINT)Device->IoQueueId;
    UINT SqDoorbellIndex = (QueueId * 2) * DbStride;
    Doorbell[SqDoorbellIndex] = (U32)Device->IoSqTail;

    LPNVME_COMPLETION Cq = (LPNVME_COMPLETION)Device->IoCq;
    UINT Head = Device->IoCqHead;
    U8 Phase = Device->IoCqPhase;

    for (UINT Loop = 0; Loop < NVME_IDENTIFY_TIMEOUT_LOOPS; Loop++) {
        LPNVME_COMPLETION Entry = &Cq[Head];
        U16 EntryStatus = Entry->Status;
        U8 EntryPhase = (U8)(EntryStatus & 0x1);
        if (EntryPhase != Phase) {
            continue;
        }

        if (Entry->CommandId != CommandId) {
            WARNING(TEXT("[NVMeSubmitIoCommand] Unexpected completion ID=%x expected=%x"),
                    (U32)Entry->CommandId,
                    (U32)CommandId);
        }

        if (CompletionOut != NULL) {
            *CompletionOut = *Entry;
        }

        Head++;
        if (Head >= Device->IoCqEntries) {
            Head = 0;
            Phase ^= 1;
        }

        Device->IoCqHead = Head;
        Device->IoCqPhase = Phase;

        UINT CqDoorbellIndex = ((QueueId * 2) + 1) * DbStride;
        Doorbell[CqDoorbellIndex] = (U32)Head;
        return TRUE;
    }

    WARNING(TEXT("[NVMeSubmitIoCommand] Timeout waiting for completion"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Submit an I/O NO-OP command and wait for completion.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeSubmitIoNoop(LPNVME_DEVICE Device) {
    if (Device == NULL || Device->IoSq == NULL || Device->IoCq == NULL) {
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_IO_OP_NOOP;
    Command.NamespaceId = 1;
    NVME_COMPLETION Completion;
    if (!NVMeSubmitIoCommand(Device, &Command, &Completion)) {
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        U16 Sc = (U16)(Status & 0xFF);
        U16 Sct = (U16)((Status >> 8) & 0x7);
        U16 Dnr = (U16)((Status >> 14) & 0x1);
        WARNING(TEXT("[NVMeSubmitIoNoop] Status=%x SCT=%x SC=%x DNR=%x"),
                (U32)Status,
                (U32)Sct,
                (U32)Sc,
                (U32)Dnr);
        return FALSE;
    }

    DEBUG(TEXT("[NVMeSubmitIoNoop] NO-OP completed on QID=%x"),
          (U32)Device->IoQueueId);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Read sectors using the I/O queue.
 *
 * @param Device NVMe device.
 * @param Lba Starting logical block address.
 * @param SectorCount Number of sectors to read.
 * @param Buffer Destination buffer (4 KiB aligned).
 * @param BufferBytes Buffer size in bytes.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeReadSectors(LPNVME_DEVICE Device, U64 Lba, U32 SectorCount, LPVOID Buffer, U32 BufferBytes) {
    if (Device == NULL || Device->IoSq == NULL || Device->IoCq == NULL) {
        return FALSE;
    }
    if (Buffer == NULL || SectorCount == 0 || BufferBytes == 0) {
        return FALSE;
    }
    if (SectorCount > (0xFFFFFFFF / SECTOR_SIZE)) {
        return FALSE;
    }

    U32 TransferBytes = SectorCount * SECTOR_SIZE;
    if (BufferBytes < TransferBytes) {
        return FALSE;
    }
    if (TransferBytes > (2 * N_4KB)) {
        WARNING(TEXT("[NVMeReadSectors] Transfer too large for PRP1/PRP2 %u bytes"),
                (U32)TransferBytes);
        return FALSE;
    }
    if (SectorCount > 0x10000) {
        WARNING(TEXT("[NVMeReadSectors] Too many sectors %u"), (U32)SectorCount);
        return FALSE;
    }

    LINEAR BufferLinear = (LINEAR)Buffer;
    if ((BufferLinear & (N_4KB - 1)) != 0) {
        WARNING(TEXT("[NVMeReadSectors] Buffer not 4 KiB aligned %p"), Buffer);
        return FALSE;
    }

    PHYSICAL BasePhys = MapLinearToPhysical(BufferLinear);
    if (BasePhys == 0) {
        return FALSE;
    }

    for (UINT Offset = 0; Offset < TransferBytes; Offset += N_4KB) {
        LINEAR Linear = BufferLinear + (LINEAR)Offset;
        PHYSICAL Physical = MapLinearToPhysical(Linear);
        if (Physical != (BasePhys + (PHYSICAL)Offset)) {
            WARNING(TEXT("[NVMeReadSectors] Buffer not contiguous at %x"), (U32)Offset);
            return FALSE;
        }
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_IO_OP_READ;
    Command.NamespaceId = 1;
    Command.Prp1Low = (U32)(BasePhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BasePhys >> 32) & 0xFFFFFFFF);
#endif

    if (TransferBytes > N_4KB) {
        PHYSICAL SecondPage = BasePhys + N_4KB;
        Command.Prp2Low = (U32)(SecondPage & 0xFFFFFFFF);
        Command.Prp2High = 0;
#ifdef __EXOS_64__
        Command.Prp2High = (U32)((SecondPage >> 32) & 0xFFFFFFFF);
#endif
    }

    Command.CommandDword10 = U64_Low32(Lba);
    Command.CommandDword11 = U64_High32(Lba);
    Command.CommandDword12 = (U32)((SectorCount - 1) & 0xFFFF);

    NVME_COMPLETION Completion;
    if (!NVMeSubmitIoCommand(Device, &Command, &Completion)) {
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        U16 Sc = (U16)(Status & 0xFF);
        U16 Sct = (U16)((Status >> 8) & 0x7);
        U16 Dnr = (U16)((Status >> 14) & 0x1);
        WARNING(TEXT("[NVMeReadSectors] Status=%x SCT=%x SC=%x DNR=%x"),
                (U32)Status,
                (U32)Sct,
                (U32)Sc,
                (U32)Dnr);
        return FALSE;
    }

    DEBUG(TEXT("[NVMeReadSectors] Read LBA=%x:%x sectors=%u"),
          U64_High32(Lba),
          U64_Low32(Lba),
          (U32)SectorCount);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Read LBA 0 and log the MBR signature.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeReadTest(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    U32 TransferBytes = SECTOR_SIZE;
    U32 RawSize = TransferBytes + N_4KB;
    LPVOID Raw = KernelHeapAlloc(RawSize);
    if (Raw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Raw;
    LINEAR AlignedBase = (LINEAR)((RawBase + (N_4KB - 1)) & ~(N_4KB - 1));
    LPVOID Buffer = (LPVOID)AlignedBase;
    MemorySet(Buffer, 0, TransferBytes);

    BOOL Result = NVMeReadSectors(Device, U64_FromU32(0), 1, Buffer, TransferBytes);
    if (Result) {
        U8* Data = (U8*)Buffer;
        U32 SigLow = (U32)Data[SECTOR_SIZE - 2];
        U32 SigHigh = (U32)Data[SECTOR_SIZE - 1];
        DEBUG(TEXT("[NVMeReadTest] MBR signature=%x %x"),
              SigLow,
              SigHigh);
    } else {
        WARNING(TEXT("[NVMeReadTest] Read LBA0 failed"));
    }

    KernelHeapFree(Raw);
    return Result;
}

/************************************************************************/

/**
 * @brief Create I/O submission and completion queues.
 *
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeCreateIoQueues(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    if (!NVMeSetupIoQueues(Device)) {
        return FALSE;
    }

    LINEAR CqOffset = (LINEAR)Device->IoCq - (LINEAR)Device->IoSq;
    PHYSICAL CqPhys = Device->IoQueuePhysical + (PHYSICAL)CqOffset;
    PHYSICAL SqPhys = Device->IoQueuePhysical;

    NVME_COMMAND Command;
    NVME_COMPLETION Completion;
    U32 CqFlags = NVME_CQ_FLAGS_PC;
    U32 InterruptVector = 0;

    if (Device->MsixEnabled) {
        CqFlags |= NVME_CQ_FLAGS_IEN;
        InterruptVector = 0;
    }

    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_CREATE_IO_CQ;
    Command.CommandId = 3;
    Command.Prp1Low = (U32)(CqPhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((CqPhys >> 32) & 0xFFFFFFFF);
#endif
    Command.CommandDword10 = (U32)Device->IoQueueId | ((Device->IoCqEntries - 1) << 16);
    Command.CommandDword11 = (InterruptVector & 0xFFFF) | CqFlags;

    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        NVMeFreeIoQueues(Device);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        U16 Sc = (U16)(Status & 0xFF);
        U16 Sct = (U16)((Status >> 8) & 0x7);
        U16 Dnr = (U16)((Status >> 14) & 0x1);
        WARNING(TEXT("[NVMeCreateIoQueues] CQ raw=%x status=%x qid=%x qsize=%x iv=%x flags=%x msix=%x vec=%x"),
                (U32)Completion.Status,
                (U32)Status,
                (U32)Device->IoQueueId,
                (U32)Device->IoCqEntries,
                (U32)(InterruptVector & 0xFFFF),
                (U32)CqFlags,
                (U32)(Device->MsixEnabled ? 1 : 0),
                (U32)Device->MsixVector);
        WARNING(TEXT("[NVMeCreateIoQueues] SQ=%p CQ=%p CqOffset=%x CqAlign=%x SqAlign=%x"),
                (LPVOID)(LINEAR)SqPhys,
                (LPVOID)(LINEAR)CqPhys,
                (U32)CqOffset,
                (U32)(CqPhys & (N_4KB - 1)),
                (U32)(SqPhys & (N_4KB - 1)));
        WARNING(TEXT("[NVMeCreateIoQueues] Create CQ status %x SCT=%x SC=%x DNR=%x"),
                (U32)Status,
                (U32)Sct,
                (U32)Sc,
                (U32)Dnr);
        NVMeFreeIoQueues(Device);
        return FALSE;
    }

    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_CREATE_IO_SQ;
    Command.CommandId = 4;
    Command.Prp1Low = (U32)(SqPhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((SqPhys >> 32) & 0xFFFFFFFF);
#endif
    Command.CommandDword10 = (U32)Device->IoQueueId | ((Device->IoSqEntries - 1) << 16);
    Command.CommandDword11 = ((U32)Device->IoQueueId << 16) | (U32)NVME_SQ_FLAGS_PC;

    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        NVMeFreeIoQueues(Device);
        return FALSE;
    }

    Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        U16 Sc = (U16)(Status & 0xFF);
        U16 Sct = (U16)((Status >> 8) & 0x7);
        U16 Dnr = (U16)((Status >> 14) & 0x1);
        WARNING(TEXT("[NVMeCreateIoQueues] Create SQ status %x SCT=%x SC=%x DNR=%x"),
                (U32)Status,
                (U32)Sct,
                (U32)Sc,
                (U32)Dnr);
        NVMeFreeIoQueues(Device);
        return FALSE;
    }

    DEBUG(TEXT("[NVMeCreateIoQueues] IO QID=%u SQ=%p CQ=%p SQE=%u CQE=%u"),
          (U32)Device->IoQueueId,
          (LPVOID)(LINEAR)SqPhys,
          (LPVOID)(LINEAR)CqPhys,
          (U32)Device->IoSqEntries,
          (U32)Device->IoCqEntries);

    return TRUE;
}

/************************************************************************/
