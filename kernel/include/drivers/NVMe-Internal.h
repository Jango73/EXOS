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

/************************************************************************/
// External functions

BOOL NVMeSetupAdminQueues(LPNVME_DEVICE Device);
void NVMeFreeAdminQueues(LPNVME_DEVICE Device);
BOOL NVMeSubmitAdminCommand(LPNVME_DEVICE Device, const NVME_COMMAND* Command, NVME_COMPLETION* CompletionOut);
BOOL NVMeIdentifyController(LPNVME_DEVICE Device);
BOOL NVMeIdentifyNamespace(LPNVME_DEVICE Device, U32 NamespaceId);
BOOL NVMeSetNumberOfQueues(LPNVME_DEVICE Device, U16 QueueCount);

BOOL NVMeSetupInterrupts(LPNVME_DEVICE Device);
BOOL NVMeCreateIoQueues(LPNVME_DEVICE Device);
BOOL NVMeSubmitIoNoop(LPNVME_DEVICE Device);
BOOL NVMeReadSectors(LPNVME_DEVICE Device, U64 Lba, U32 SectorCount, LPVOID Buffer, U32 BufferBytes);
BOOL NVMeReadTest(LPNVME_DEVICE Device);
void NVMeFreeIoQueues(LPNVME_DEVICE Device);

volatile U32* NVMeGetDoorbellBase(LPNVME_DEVICE Device);

/************************************************************************/

#endif  // DRIVERS_NVME_INTERNAL_H_INCLUDED
