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


    NVMe (admin)

\************************************************************************/

#include "drivers/NVMe-Internal.h"

/************************************************************************/

/**
 * @brief Free admin queue memory.
 *
 * @param Device NVMe device.
 */
void NVMeFreeAdminQueues(LPNVME_DEVICE Device) {
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
BOOL NVMeSetupAdminQueues(LPNVME_DEVICE Device) {
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
    LINEAR AlignedBase = (LINEAR)((RawBase + (NVME_ADMIN_QUEUE_ALIGNMENT - 1)) &
        ~(NVME_ADMIN_QUEUE_ALIGNMENT - 1));
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
    Device->AdminCqPhase = 1;

    return TRUE;
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
BOOL NVMeSubmitAdminCommand(LPNVME_DEVICE Device, const NVME_COMMAND* Command, NVME_COMPLETION* CompletionOut) {
    if (Device == NULL || Command == NULL || Device->AdminSq == NULL || Device->AdminCq == NULL) {
        WARNING(TEXT("[NVMeSubmitAdminCommand] Invalid parameters"));
        return FALSE;
    }

    UINT Tail = Device->AdminSqTail;
    LPNVME_COMMAND Sq = (LPNVME_COMMAND)Device->AdminSq;
    volatile LPNVME_COMPLETION Cq = (volatile LPNVME_COMPLETION)Device->AdminCq;

    MemoryCopy(&Sq[Tail], Command, sizeof(NVME_COMMAND));
    Device->AdminSqTail = (Tail + 1) % Device->AdminSqEntries;

    volatile U32* Doorbell = NVMeGetDoorbellBase(Device);
    if (Doorbell == NULL) {
        WARNING(TEXT("[NVMeSubmitAdminCommand] Doorbell base is null"));
        return FALSE;
    }

    UINT DbStride = (UINT)(Device->DoorbellStride / 4);
    __asm__ __volatile__("" ::: "memory");
    Doorbell[0] = (U32)Device->AdminSqTail;

    UINT Head = Device->AdminCqHead;
    U8 Phase = Device->AdminCqPhase;

    for (UINT Loop = 0; Loop < NVME_IDENTIFY_TIMEOUT_LOOPS; Loop++) {
        volatile LPNVME_COMPLETION Entry = &Cq[Head];
        U16 Status = Entry->Status;
        U8 EntryPhase = (U8)(Status & 0x1);
        if (EntryPhase == Phase) {
            U16 EntryCommandId = Entry->CommandId;
            if (CompletionOut != NULL) {
                CompletionOut->Result = Entry->Result;
                CompletionOut->Reserved = Entry->Reserved;
                CompletionOut->SubmissionQueueHead = Entry->SubmissionQueueHead;
                CompletionOut->SubmissionQueueId = Entry->SubmissionQueueId;
                CompletionOut->CommandId = EntryCommandId;
                CompletionOut->Status = Status;
            }

            Head++;
            if (Head >= Device->AdminCqEntries) {
                Head = 0;
                Phase ^= 1;
            }

            Device->AdminCqHead = Head;
            Device->AdminCqPhase = Phase;
            Doorbell[DbStride] = (U32)Head;

            if (EntryCommandId != Command->CommandId) {
                WARNING(TEXT("[NVMeSubmitAdminCommand] Completion command id %x (expected %x)"),
                        (U32)EntryCommandId,
                        (U32)Command->CommandId);
            }
            return TRUE;
        }

        __asm__ __volatile__("pause");
    }

    WARNING(TEXT("[NVMeSubmitAdminCommand] Timeout opcode=%x command_id=%x head=%u tail=%u"),
            (U32)Command->Opcode,
            (U32)Command->CommandId,
            Head,
            Device->AdminSqTail);
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
    LINEAR AlignedBase = (LINEAR)((RawBase + (N_4KB - 1)) & ~(N_4KB - 1));
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
BOOL NVMeIdentifyController(LPNVME_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    LPVOID Buffer = NULL;
    LPVOID Raw = NULL;
    if (!NVMeAllocateIdentifyBuffer(&Buffer, &Raw)) {
        return FALSE;
    }

    PHYSICAL BufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (BufferPhys == 0 || (BufferPhys & (N_4KB - 1)) != 0) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_IDENTIFY;
    Command.CommandId = 1;
    Command.NamespaceId = 0;
    Command.Prp1Low = (U32)(BufferPhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BufferPhys >> 32) & 0xFFFFFFFF);
#endif
    Command.CommandDword10 = 1;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        KernelHeapFree(Raw);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
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
 * @param NumSectorsOut Receives namespace size in sectors (optional).
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeIdentifyNamespace(LPNVME_DEVICE Device, U32 NamespaceId, U64* NumSectorsOut) {
    if (Device == NULL || NamespaceId == 0) {
        WARNING(TEXT("[NVMeIdentifyNamespace] Invalid parameters NSID=%u"), NamespaceId);
        return FALSE;
    }

    LPVOID Buffer = NULL;
    LPVOID Raw = NULL;
    if (!NVMeAllocateIdentifyBuffer(&Buffer, &Raw)) {
        WARNING(TEXT("[NVMeIdentifyNamespace] Buffer allocation failed NSID=%u"), NamespaceId);
        return FALSE;
    }

    PHYSICAL BufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (BufferPhys == 0 || (BufferPhys & (N_4KB - 1)) != 0) {
        WARNING(TEXT("[NVMeIdentifyNamespace] Invalid identify buffer mapping NSID=%u phys=%x%08x"),
                NamespaceId,
                (U32)U64_High32(BufferPhys),
                (U32)U64_Low32(BufferPhys));
        KernelHeapFree(Raw);
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_IDENTIFY;
    Command.CommandId = 2;
    Command.NamespaceId = NamespaceId;
    Command.Prp1Low = (U32)(BufferPhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BufferPhys >> 32) & 0xFFFFFFFF);
#endif
    Command.CommandDword10 = 0;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        WARNING(TEXT("[NVMeIdentifyNamespace] Submit identify failed NSID=%u"), NamespaceId);
        KernelHeapFree(Raw);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
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
          (U32)U64_High32(Nsze),
          (U32)U64_Low32(Nsze));

    if (NumSectorsOut != NULL) {
        *NumSectorsOut = Nsze;
    }

    KernelHeapFree(Raw);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Identify active namespace list.
 *
 * @param Device NVMe device.
 * @param NamespaceIds Output array of namespace IDs.
 * @param MaxIds Capacity of NamespaceIds array.
 * @param CountOut Receives number of IDs written.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeIdentifyNamespaceList(LPNVME_DEVICE Device, U32* NamespaceIds, UINT MaxIds, UINT* CountOut) {
    if (Device == NULL || NamespaceIds == NULL || CountOut == NULL || MaxIds == 0) {
        WARNING(TEXT("[NVMeIdentifyNamespaceList] Invalid parameters"));
        return FALSE;
    }

    *CountOut = 0;

    LPVOID Buffer = NULL;
    LPVOID Raw = NULL;
    if (!NVMeAllocateIdentifyBuffer(&Buffer, &Raw)) {
        WARNING(TEXT("[NVMeIdentifyNamespaceList] Buffer allocation failed"));
        return FALSE;
    }

    PHYSICAL BufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (BufferPhys == 0 || (BufferPhys & (N_4KB - 1)) != 0) {
        WARNING(TEXT("[NVMeIdentifyNamespaceList] Invalid identify buffer mapping phys=%x%08x"),
                (U32)U64_High32(BufferPhys),
                (U32)U64_Low32(BufferPhys));
        KernelHeapFree(Raw);
        return FALSE;
    }

    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_IDENTIFY;
    Command.CommandId = 6;
    Command.NamespaceId = 0;
    Command.Prp1Low = (U32)(BufferPhys & 0xFFFFFFFF);
    Command.Prp1High = 0;
#ifdef __EXOS_64__
    Command.Prp1High = (U32)((BufferPhys >> 32) & 0xFFFFFFFF);
#endif
    Command.CommandDword10 = 2;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        WARNING(TEXT("[NVMeIdentifyNamespaceList] Submit identify list failed"));
        KernelHeapFree(Raw);
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        WARNING(TEXT("[NVMeIdentifyNamespaceList] Completion status %x"), (U32)Status);
        KernelHeapFree(Raw);
        return FALSE;
    }

    U32* Data = (U32*)Buffer;
    UINT Count = 0;
    for (UINT Index = 0; Index < MaxIds; Index++) {
        U32 NamespaceId = Data[Index];
        if (NamespaceId == 0) {
            break;
        }
        NamespaceIds[Count] = NamespaceId;
        Count++;
    }

    *CountOut = Count;
    KernelHeapFree(Raw);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Request the number of I/O queues supported.
 *
 * @param Device NVMe device.
 * @param QueueCount Requested queue count (1-based).
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeSetNumberOfQueues(LPNVME_DEVICE Device, U16 QueueCount) {
    if (Device == NULL || QueueCount == 0) {
        return FALSE;
    }

    U16 Requested = (U16)(QueueCount - 1);
    NVME_COMMAND Command;
    MemorySet(&Command, 0, sizeof(Command));
    Command.Opcode = NVME_ADMIN_OP_SET_FEATURES;
    Command.CommandId = 5;
    Command.CommandDword10 = (U32)NVME_FEATURE_NUMBER_OF_QUEUES;
    Command.CommandDword11 = ((U32)Requested << 16) | (U32)Requested;

    NVME_COMPLETION Completion;
    if (!NVMeSubmitAdminCommand(Device, &Command, &Completion)) {
        return FALSE;
    }

    U16 Status = (U16)(Completion.Status >> 1);
    if (Status != 0) {
        U16 Sc = (U16)(Status & 0xFF);
        U16 Sct = (U16)((Status >> 8) & 0x7);
        U16 Dnr = (U16)((Status >> 14) & 0x1);
        WARNING(TEXT("[NVMeSetNumberOfQueues] Status=%x SCT=%x SC=%x DNR=%x"),
                (U32)Status,
                (U32)Sct,
                (U32)Sc,
                (U32)Dnr);
        return FALSE;
    }

    U32 Result = Completion.Result;
    U16 MaxSq = (U16)(Result & 0xFFFF);
    U16 MaxCq = (U16)((Result >> 16) & 0xFFFF);
    DEBUG(TEXT("[NVMeSetNumberOfQueues] MaxSQ=%x MaxCQ=%x"),
          (U32)MaxSq,
          (U32)MaxCq);

    return TRUE;
}

/************************************************************************/
