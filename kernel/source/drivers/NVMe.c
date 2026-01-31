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

/************************************************************************/
// Forward declarations

static UINT NVMeCommands(UINT Function, UINT Param);
static UINT NVMeProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE NVMeAttach(LPPCI_DEVICE PciDevice);
static BOOL NVMeGetBar0Physical(LPPCI_DEVICE Device, PHYSICAL* BaseOut, U32* SizeOut);

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

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/
