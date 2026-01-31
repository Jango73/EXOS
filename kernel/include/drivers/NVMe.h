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

#ifndef DRIVERS_NVME_H_INCLUDED
#define DRIVERS_NVME_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "drivers/PCI.h"

/************************************************************************/
// Macros

#define NVME_PCI_CLASS 0x01
#define NVME_PCI_SUBCLASS 0x08
#define NVME_PCI_PROG_IF 0x02

#define NVME_REG_CAP 0x00
#define NVME_REG_VS 0x08
#define NVME_REG_INTMS 0x0C
#define NVME_REG_INTMC 0x10
#define NVME_REG_CC 0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA 0x24
#define NVME_REG_ASQ 0x28
#define NVME_REG_ACQ 0x30

/************************************************************************/
// Type definitions

typedef struct tag_NVME_DEVICE {
    PCI_DEVICE_FIELDS

    LINEAR MmioBase;
    U32 MmioSize;

    LINEAR AdminQueueBase;
    LPVOID AdminQueueRaw;
    PHYSICAL AdminQueuePhysical;
    U32 AdminQueueSize;
    U32 AdminSqEntries;
    U32 AdminCqEntries;
    U8* AdminSq;
    U8* AdminCq;
} NVME_DEVICE, *LPNVME_DEVICE;

/************************************************************************/

#endif  // DRIVERS_NVME_H_INCLUDED
