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


    NVMe (internal)

\************************************************************************/

#ifndef DRIVERS_NVME_INTERNAL_H_INCLUDED
#define DRIVERS_NVME_INTERNAL_H_INCLUDED

/************************************************************************/

#include "drivers/NVMe-Core.h"
#include "Clock.h"
#include "Disk.h"
#include "Driver.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/DeviceInterrupt.h"
#include "drivers/LocalAPIC.h"
#include "drivers/PCI.h"

/************************************************************************/
// Macros

#define NVME_ADMIN_QUEUE_ENTRIES 64
#define NVME_ADMIN_SQ_ENTRY_SIZE 64
#define NVME_ADMIN_CQ_ENTRY_SIZE 16
#define NVME_ADMIN_QUEUE_ALIGNMENT N_4KB
#define NVME_IO_QUEUE_ENTRIES 16
#define NVME_IO_SQ_ENTRY_SIZE 64
#define NVME_IO_CQ_ENTRY_SIZE 16
#define NVME_IO_QUEUE_ALIGNMENT N_4KB
#define NVME_READY_TIMEOUT_LOOPS 1000000
#define NVME_IDENTIFY_TIMEOUT_LOOPS 1000000
#define NVME_COMMAND_TIMEOUT_MS 200
#define NVME_COMMAND_TIMEOUT_LOOPS 0x10000000

#ifndef NVME_POLLING_ONLY
#define NVME_POLLING_ONLY 1
#endif

/************************************************************************/
// Type definitions

typedef struct tag_NVME_DISK {
    STORAGE_UNIT Header;
    LPNVME_DEVICE Controller;
    U32 NamespaceId;
    U64 NumSectors;
    U32 BytesPerSector;
    U32 Access;
} NVME_DISK, *LPNVME_DISK;

/************************************************************************/
// External functions

BOOL NVMeSetupAdminQueues(LPNVME_DEVICE Device);
void NVMeFreeAdminQueues(LPNVME_DEVICE Device);
BOOL NVMeSubmitAdminCommand(LPNVME_DEVICE Device, const NVME_COMMAND* Command, NVME_COMPLETION* CompletionOut);
BOOL NVMeIdentifyController(LPNVME_DEVICE Device);
BOOL NVMeIdentifyNamespace(LPNVME_DEVICE Device, U32 NamespaceId, U64* NumSectorsOut, U32* BytesPerSectorOut);
BOOL NVMeIdentifyNamespaceList(LPNVME_DEVICE Device, U32* NamespaceIds, UINT MaxIds, UINT* CountOut);
BOOL NVMeSetNumberOfQueues(LPNVME_DEVICE Device, U16 QueueCount);

BOOL NVMeSetupInterrupts(LPNVME_DEVICE Device);
BOOL NVMeCreateIoQueues(LPNVME_DEVICE Device);
BOOL NVMeSubmitIoNoop(LPNVME_DEVICE Device);
BOOL NVMeReadSectors(LPNVME_DEVICE Device, U32 NamespaceId, U64 Lba, U32 SectorCount, LPVOID Buffer,
                     U32 BufferBytes);
BOOL NVMeWriteSectors(LPNVME_DEVICE Device, U32 NamespaceId, U64 Lba, U32 SectorCount, LPCVOID Buffer,
                      U32 BufferBytes);
BOOL NVMeReadTest(LPNVME_DEVICE Device);
void NVMeFreeIoQueues(LPNVME_DEVICE Device);

volatile U32* NVMeGetDoorbellBase(LPNVME_DEVICE Device);

void NVMeInitDiskDriver(LPNVME_DEVICE Device);
BOOL NVMeRegisterNamespaces(LPNVME_DEVICE Device);

/************************************************************************/

#endif  // DRIVERS_NVME_INTERNAL_H_INCLUDED
