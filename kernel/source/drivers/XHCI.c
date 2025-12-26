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


    xHCI

\************************************************************************/

#include "drivers/XHCI.h"

#include "Base.h"
#include "CoreString.h"
#include "DriverEnum.h"
#include "Kernel.h"
#include "KernelData.h"
#include "Log.h"
#include "Memory.h"
#include "User.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// xHCI capability registers

#define XHCI_CAPLENGTH 0x00
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCSPARAMS3 0x0C
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
#define XHCI_HCCPARAMS2 0x1C

#define XHCI_HCSPARAMS1_MAXSLOTS_MASK 0x000000FF
#define XHCI_HCSPARAMS1_MAXINTRS_MASK 0x0007FF00
#define XHCI_HCSPARAMS1_MAXINTRS_SHIFT 8
#define XHCI_HCSPARAMS1_MAXPORTS_MASK 0xFF000000
#define XHCI_HCSPARAMS1_MAXPORTS_SHIFT 24
#define XHCI_HCSPARAMS1_PPC 0x00000010

#define XHCI_HCCPARAMS1_AC64 0x00000001

/************************************************************************/
// xHCI operational registers (offset from operational base)

#define XHCI_OP_USBCMD 0x00
#define XHCI_OP_USBSTS 0x04
#define XHCI_OP_PAGESIZE 0x08
#define XHCI_OP_DNCTRL 0x14
#define XHCI_OP_CRCR 0x18
#define XHCI_OP_DCBAAP 0x30
#define XHCI_OP_CONFIG 0x38

#define XHCI_USBCMD_RS 0x00000001
#define XHCI_USBCMD_HCRST 0x00000002

#define XHCI_USBSTS_HCH 0x00000001
#define XHCI_USBSTS_CNR 0x00000800

/************************************************************************/
// xHCI port registers (offset from operational base)

#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC_STRIDE 0x10

#define XHCI_PORTSC_CCS 0x00000001
#define XHCI_PORTSC_PED 0x00000002
#define XHCI_PORTSC_PP 0x00000200
#define XHCI_PORTSC_PLS_MASK 0x000001E0
#define XHCI_PORTSC_SPEED_MASK 0x00003C00
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_W1C_MASK 0x00FE0000

/************************************************************************/
// xHCI runtime registers

#define XHCI_RT_MFINDEX 0x00
#define XHCI_RT_INTERRUPTER_BASE 0x20
#define XHCI_RT_INTERRUPTER_STRIDE 0x20

#define XHCI_IMAN 0x00
#define XHCI_IMOD 0x04
#define XHCI_ERSTSZ 0x08
#define XHCI_ERSTBA 0x10
#define XHCI_ERDP 0x18

/************************************************************************/
// TRB definitions

#define XHCI_TRB_TYPE_SHIFT 10
#define XHCI_TRB_TYPE_LINK 6

#define XHCI_TRB_CYCLE 0x00000001
#define XHCI_TRB_TOGGLE_CYCLE 0x00000002

#define XHCI_COMMAND_RING_TRBS 256
#define XHCI_EVENT_RING_TRBS 256

/************************************************************************/

#define XHCI_RESET_TIMEOUT 1000000
#define XHCI_HALT_TIMEOUT 1000000
#define XHCI_RUN_TIMEOUT 1000000

/************************************************************************/

typedef struct tag_XHCI_TRB {
    U32 Dword0;
    U32 Dword1;
    U32 Dword2;
    U32 Dword3;
} XHCI_TRB, *LPXHCI_TRB;

typedef struct tag_XHCI_ERST_ENTRY {
    U64 SegmentBase;
    U16 SegmentSize;
    U16 Reserved;
    U32 Reserved2;
} XHCI_ERST_ENTRY, *LPXHCI_ERST_ENTRY;

struct PACKED tag_XHCI_DEVICE {
    PCI_DEVICE_FIELDS

    LINEAR MmioBase;
    U32 MmioSize;

    U8 CapLength;
    U16 HciVersion;
    U8 MaxSlots;
    U8 MaxPorts;
    U16 MaxInterrupters;
    U32 HccParams1;

    LINEAR OpBase;
    LINEAR RuntimeBase;
    LINEAR DoorbellBase;

    PHYSICAL DcbaaPhysical;
    LINEAR DcbaaLinear;

    PHYSICAL CommandRingPhysical;
    LINEAR CommandRingLinear;
    U32 CommandRingCycleState;

    PHYSICAL EventRingPhysical;
    LINEAR EventRingLinear;
    PHYSICAL EventRingTablePhysical;
    LINEAR EventRingTableLinear;
};

/************************************************************************/

#pragma pack(pop)

/************************************************************************/
// MMIO access

/**
 * @brief Read a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @return Register value.
 */
static U32 XHCI_Read32(LINEAR Base, U32 Offset) {
    return *(volatile U32 *)((U8 *)Base + Offset);
}

/************************************************************************/

/**
 * @brief Write a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
static void XHCI_Write32(LINEAR Base, U32 Offset, U32 Value) {
    *(volatile U32 *)((U8 *)Base + Offset) = Value;
}

/************************************************************************/

/**
 * @brief Write a 64-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
static void XHCI_Write64(LINEAR Base, U32 Offset, U64 Value) {
    XHCI_Write32(Base, Offset, U64_Low32(Value));
    XHCI_Write32(Base, (U32)(Offset + 4), U64_High32(Value));
}

/************************************************************************/

/**
 * @brief Busy-wait for a register to match a value.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Mask Mask applied to register.
 * @param Value Expected value after masking.
 * @param Timeout Loop bound.
 * @return TRUE on success, FALSE on timeout.
 */
static BOOL XHCI_WaitForRegister(LINEAR Base, U32 Offset, U32 Mask, U32 Value, U32 Timeout) {
    U32 Count = 0;
    while (Count < Timeout) {
        if ((XHCI_Read32(Base, Offset) & Mask) == Value) {
            return TRUE;
        }
        Count++;
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate and map a single physical page.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_AllocPage(LPCSTR Tag, PHYSICAL *PhysicalOut, LINEAR *LinearOut) {
    if (PhysicalOut == NULL || LinearOut == NULL) {
        return FALSE;
    }

    PHYSICAL Physical = AllocPhysicalPage();
    if (Physical == 0) {
        return FALSE;
    }

    LINEAR Linear = AllocKernelRegion(Physical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, Tag);
    if (Linear == 0) {
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Free xHCI allocations and MMIO mapping.
 * @param Device xHCI device.
 */
static void XHCI_FreeResources(LPXHCI_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        if (Device->EventRingTableLinear) {
            FreeRegion(Device->EventRingTableLinear, PAGE_SIZE);
            Device->EventRingTableLinear = 0;
        }
        if (Device->EventRingTablePhysical) {
            FreePhysicalPage(Device->EventRingTablePhysical);
            Device->EventRingTablePhysical = 0;
        }
        if (Device->EventRingLinear) {
            FreeRegion(Device->EventRingLinear, PAGE_SIZE);
            Device->EventRingLinear = 0;
        }
        if (Device->EventRingPhysical) {
            FreePhysicalPage(Device->EventRingPhysical);
            Device->EventRingPhysical = 0;
        }
        if (Device->CommandRingLinear) {
            FreeRegion(Device->CommandRingLinear, PAGE_SIZE);
            Device->CommandRingLinear = 0;
        }
        if (Device->CommandRingPhysical) {
            FreePhysicalPage(Device->CommandRingPhysical);
            Device->CommandRingPhysical = 0;
        }
        if (Device->DcbaaLinear) {
            FreeRegion(Device->DcbaaLinear, PAGE_SIZE);
            Device->DcbaaLinear = 0;
        }
        if (Device->DcbaaPhysical) {
            FreePhysicalPage(Device->DcbaaPhysical);
            Device->DcbaaPhysical = 0;
        }
        if (Device->MmioBase != 0 && Device->MmioSize != 0) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            Device->MmioBase = 0;
            Device->MmioSize = 0;
        }
    }
}

/************************************************************************/

/**
 * @brief Convert an xHCI speed ID to a human readable name.
 * @param SpeedId Raw PORTSC speed value.
 * @return Speed string.
 */
static LPCSTR XHCI_SpeedToString(U32 SpeedId) {
    switch (SpeedId) {
        case 1:
            return TEXT("FS");
        case 2:
            return TEXT("LS");
        case 3:
            return TEXT("HS");
        case 4:
            return TEXT("SS");
        case 5:
            return TEXT("SS+");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/

/**
 * @brief Read a port status register.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return PORTSC value.
 */
static U32 XHCI_ReadPortStatus(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    return XHCI_Read32(Device->OpBase, Offset);
}

/************************************************************************/

/**
 * @brief Power on a port if supported by the controller.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 */
static void XHCI_PowerPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_PP) != 0) {
        return;
    }

    U32 WriteValue = PortStatus | XHCI_PORTSC_PP;
    WriteValue &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, WriteValue);
}

/************************************************************************/

/**
 * @brief Log port status to kernel log.
 * @param Device xHCI device.
 */
static void XHCI_LogPorts(LPXHCI_DEVICE Device) {
    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        BOOL Enabled = (PortStatus & XHCI_PORTSC_PED) != 0;

        DEBUG(TEXT("[XHCI_LogPorts] Port %u CCS=%u PED=%u Speed=%s Raw=%x"),
              PortIndex + 1,
              Connected ? 1U : 0U,
              Enabled ? 1U : 0U,
              XHCI_SpeedToString(SpeedId),
              PortStatus);
    }
}

/************************************************************************/

/**
 * @brief Initialize the command ring.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitCommandRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_CommandRing"), &Device->CommandRingPhysical, &Device->CommandRingLinear)) {
        ERROR(TEXT("[XHCI_InitCommandRing] Command ring allocation failed"));
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->CommandRingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_COMMAND_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(Device->CommandRingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    Device->CommandRingCycleState = 1;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the event ring and interrupter 0.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitEventRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_EventRing"), &Device->EventRingPhysical, &Device->EventRingLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] Event ring allocation failed"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_EventRingTable"), &Device->EventRingTablePhysical, &Device->EventRingTableLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] ERST allocation failed"));
        return FALSE;
    }

    LPXHCI_ERST_ENTRY Entries = (LPXHCI_ERST_ENTRY)Device->EventRingTableLinear;
    MemorySet(Entries, 0, PAGE_SIZE);
    Entries[0].SegmentBase = U64_FromUINT(Device->EventRingPhysical);
    Entries[0].SegmentSize = XHCI_EVENT_RING_TRBS;
    Entries[0].Reserved = 0;
    Entries[0].Reserved2 = 0;

    LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, 0);
    XHCI_Write32(InterrupterBase, XHCI_IMOD, 0);
    XHCI_Write32(InterrupterBase, XHCI_ERSTSZ, 1);
    XHCI_Write64(InterrupterBase, XHCI_ERSTBA, U64_FromUINT(Device->EventRingTablePhysical));
    XHCI_Write64(InterrupterBase, XHCI_ERDP, U64_FromUINT(Device->EventRingPhysical));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset and start the xHCI controller.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_ResetAndStart(LPXHCI_DEVICE Device) {
    U32 Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command &= ~XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, XHCI_HALT_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Halt timeout"));
        return FALSE;
    }

    Command |= XHCI_USBCMD_HCRST;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Reset bit timeout"));
        return FALSE;
    }

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Controller not ready"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DCBAA"), &Device->DcbaaPhysical, &Device->DcbaaLinear)) {
        ERROR(TEXT("[XHCI_ResetAndStart] DCBAA allocation failed"));
        return FALSE;
    }

    if (!XHCI_InitCommandRing(Device)) {
        return FALSE;
    }

    if (!XHCI_InitEventRing(Device)) {
        return FALSE;
    }

    XHCI_Write64(Device->OpBase, XHCI_OP_DCBAAP, U64_FromUINT(Device->DcbaaPhysical));

    {
        U64 Crcr = U64_FromUINT(Device->CommandRingPhysical);
        U32 Low = U64_Low32(Crcr) | XHCI_TRB_CYCLE;
        U32 High = U64_High32(Crcr);
        XHCI_Write32(Device->OpBase, XHCI_OP_CRCR, Low);
        XHCI_Write32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4), High);
    }

    XHCI_Write32(Device->OpBase, XHCI_OP_CONFIG, Device->MaxSlots);

    Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command |= XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0, XHCI_RUN_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Run timeout"));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize xHCI MMIO offsets and controller capabilities.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitController(LPXHCI_DEVICE Device) {
    U32 CapLengthReg = XHCI_Read32(Device->MmioBase, XHCI_CAPLENGTH);
    Device->CapLength = (U8)(CapLengthReg & MAX_U8);
    Device->HciVersion = (U16)((CapLengthReg >> 16) & 0xFFFF);

    U32 HcsParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCSPARAMS1);
    Device->MaxSlots = (U8)(HcsParams1 & XHCI_HCSPARAMS1_MAXSLOTS_MASK);
    Device->MaxInterrupters = (U16)((HcsParams1 & XHCI_HCSPARAMS1_MAXINTRS_MASK) >> XHCI_HCSPARAMS1_MAXINTRS_SHIFT);
    Device->MaxPorts = (U8)((HcsParams1 & XHCI_HCSPARAMS1_MAXPORTS_MASK) >> XHCI_HCSPARAMS1_MAXPORTS_SHIFT);
    Device->HccParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCCPARAMS1);

    Device->OpBase = Device->MmioBase + Device->CapLength;

    U32 DbOff = XHCI_Read32(Device->MmioBase, XHCI_DBOFF);
    U32 RtOff = XHCI_Read32(Device->MmioBase, XHCI_RTSOFF);
    Device->DoorbellBase = Device->MmioBase + (DbOff & 0xFFFFFFFC);
    Device->RuntimeBase = Device->MmioBase + (RtOff & 0xFFFFFFE0);

    DEBUG(TEXT("[XHCI_InitController] CapLen=%u HciVer=%x MaxSlots=%u MaxPorts=%u MaxIntrs=%u"),
          Device->CapLength,
          Device->HciVersion,
          Device->MaxSlots,
          Device->MaxPorts,
          Device->MaxInterrupters);

    U32 PageSize = XHCI_Read32(Device->OpBase, XHCI_OP_PAGESIZE);
    DEBUG(TEXT("[XHCI_InitController] PageSize bitmap=%x"), PageSize);

    if ((Device->HccParams1 & XHCI_HCCPARAMS1_AC64) == 0) {
        DEBUG(TEXT("[XHCI_InitController] 64-bit addressing not supported"));
    }

    if (!XHCI_ResetAndStart(Device)) {
        return FALSE;
    }

    if ((HcsParams1 & XHCI_HCSPARAMS1_PPC) != 0) {
        for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
            XHCI_PowerPort(Device, PortIndex);
        }
    }

    XHCI_LogPorts(Device);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Probe callback used by PCI subsystem.
 * @param PciInfo PCI device info.
 * @return DF_RET_SUCCESS when supported, DF_RET_NOTIMPL otherwise.
 */
static U32 XHCI_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo == NULL) return DF_RET_BADPARAM;
    if (PciInfo->BaseClass != XHCI_CLASS_SERIAL_BUS) return DF_RET_NOTIMPL;
    if (PciInfo->SubClass != XHCI_SUBCLASS_USB) return DF_RET_NOTIMPL;
    if (PciInfo->ProgIF != XHCI_PROGIF_XHCI) return DF_RET_NOTIMPL;
    return DF_RET_SUCCESS;
}

/************************************************************************/

/**
 * @brief Load callback for driver.
 * @return DF_RET_SUCCESS.
 */
static U32 XHCI_OnLoad(void) { return DF_RET_SUCCESS; }

/************************************************************************/

/**
 * @brief Unload callback for driver.
 * @return DF_RET_SUCCESS.
 */
static U32 XHCI_OnUnload(void) { return DF_RET_SUCCESS; }

/************************************************************************/

/**
 * @brief Version callback for driver.
 * @return Encoded version.
 */
static U32 XHCI_OnGetVersion(void) { return MAKE_VERSION(1, 0); }

/************************************************************************/

/**
 * @brief Capabilities callback for driver.
 * @return Capability bitmask.
 */
static U32 XHCI_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Last function callback.
 * @return Last function ID.
 */
static U32 XHCI_OnGetLastFunc(void) { return DF_PROBE; }

/************************************************************************/

static U32 XHCI_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RET_BADPARAM;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) ||
        Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RET_BADPARAM;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_XHCI_PORT) {
        return DF_RET_NOTIMPL;
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return DF_RET_NO_MORE;
    }

    UINT MatchIndex = 0;
    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Device = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            for (UINT PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                if (MatchIndex == Next->Query->Index) {
                    U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
                    U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
                    UINT Connected = (PortStatus & XHCI_PORTSC_CCS) ? 1U : 0U;
                    UINT Enabled = (PortStatus & XHCI_PORTSC_PED) ? 1U : 0U;

                    DRIVER_ENUM_XHCI_PORT Data;
                    MemorySet(&Data, 0, sizeof(Data));
                    Data.Bus = Device->Info.Bus;
                    Data.Dev = Device->Info.Dev;
                    Data.Func = Device->Info.Func;
                    Data.PortNumber = (U8)(PortIndex + 1);
                    Data.PortStatus = PortStatus;
                    Data.SpeedId = SpeedId;
                    Data.Connected = Connected;
                    Data.Enabled = Enabled;

                    MemorySet(Next->Item, 0, sizeof(DRIVER_ENUM_ITEM));
                    Next->Item->Header.Size = sizeof(DRIVER_ENUM_ITEM);
                    Next->Item->Header.Version = EXOS_ABI_VERSION;
                    Next->Item->Domain = ENUM_DOMAIN_XHCI_PORT;
                    Next->Item->Index = Next->Query->Index;
                    Next->Item->DataSize = sizeof(Data);
                    MemoryCopy(Next->Item->Data, &Data, sizeof(Data));

                    Next->Query->Index++;
                    return DF_RET_SUCCESS;
                }

                MatchIndex++;
            }
        }
    }

    return DF_RET_NO_MORE;
}

/************************************************************************/

static U32 XHCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RET_BADPARAM;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RET_BADPARAM;
    }

    if (Pretty->Item->Domain != ENUM_DOMAIN_XHCI_PORT ||
        Pretty->Item->DataSize < sizeof(DRIVER_ENUM_XHCI_PORT)) {
        return DF_RET_BADPARAM;
    }

    const DRIVER_ENUM_XHCI_PORT* Data = (const DRIVER_ENUM_XHCI_PORT*)Pretty->Item->Data;
    StringPrintFormat(Pretty->Buffer,
                      TEXT("xHCI %x:%x.%u Port %u CCS=%u PED=%u Speed=%s Raw=%x"),
                      (U32)Data->Bus,
                      (U32)Data->Dev,
                      (U32)Data->Func,
                      (U32)Data->PortNumber,
                      Data->Connected,
                      Data->Enabled,
                      XHCI_SpeedToString(Data->SpeedId),
                      Data->PortStatus);

    return DF_RET_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RET_* code.
 */
static UINT XHCI_Commands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return XHCI_OnLoad();
        case DF_UNLOAD:
            return XHCI_OnUnload();
        case DF_GETVERSION:
            return XHCI_OnGetVersion();
        case DF_GETCAPS:
            return XHCI_OnGetCaps();
        case DF_GETLASTFUNC:
            return XHCI_OnGetLastFunc();
        case DF_PROBE:
            return XHCI_OnProbe((const PCI_INFO *)(LPVOID)Param);
        case DF_ENUM_NEXT:
            return XHCI_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Param);
        case DF_ENUM_PRETTY:
            return XHCI_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Param);
    }

    return DF_RET_NOTIMPL;
}

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDevice PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE XHCI_Attach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    DEBUG(TEXT("[XHCI_Attach] New device %x:%x.%u"),
          (U32)PciDevice->Info.Bus,
          (U32)PciDevice->Info.Dev,
          (U32)PciDevice->Info.Func);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)KernelHeapAlloc(sizeof(XHCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(XHCI_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));

    U32 Bar0Raw = Device->Info.BAR[0];
    U32 Bar1Raw = Device->Info.BAR[1];
    U32 Bar0Base = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    BOOL Is64Bit = ((Bar0Raw & 0x6) == 0x4);

    if (Is64Bit && Bar1Raw != 0) {
        ERROR(TEXT("[XHCI_Attach] 64-bit BAR above 4GB not supported (BAR1=%x)"), Bar1Raw);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Bar0Base == 0 || Bar0Size == 0) {
        ERROR(TEXT("[XHCI_Attach] Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(Bar0Base, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == 0) {
        ERROR(TEXT("[XHCI_Attach] MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!XHCI_InitController(Device)) {
        ERROR(TEXT("[XHCI_Attach] Controller init failed"));
        XHCI_FreeResources(Device);
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[XHCI_Attach] Attached MMIO=%p Size=%u MaxPorts=%u"),
          Device->MmioBase,
          Device->MmioSize,
          Device->MaxPorts);

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

static DRIVER_MATCH XHCI_MatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, XHCI_CLASS_SERIAL_BUS, XHCI_SUBCLASS_USB, XHCI_PROGIF_XHCI}
};

PCI_DRIVER DATA_SECTION XHCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "xHCI",
    .Command = XHCI_Commands,
    .EnumDomainCount = 1,
    .EnumDomains = {ENUM_DOMAIN_XHCI_PORT},
    .Matches = XHCI_MatchTable,
    .MatchCount = sizeof(XHCI_MatchTable) / sizeof(XHCI_MatchTable[0]),
    .Attach = XHCI_Attach
};
