
/***************************************************************************\

    EXOS Kernel - PCI Bus Layer
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/PCI.h"

#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/String.h"

/***************************************************************************/
// PCI config mechanism #1 (0xCF8/0xCFC)

#define PCI_CONFIG_ADDRESS_PORT 0x0CF8
#define PCI_CONFIG_DATA_PORT 0x0CFC

/* Build the 32-bit address for type-1 config cycles */
#define PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset)                                                        \
    (0x80000000U | (((U32)(Bus)&0xFFU) << 16) | (((U32)(Device)&0x1FU) << 11) | (((U32)(Function)&0x07U) << 8) | \
     ((U32)(Offset)&0xFCU))

/***************************************************************************/
// Forward declarations

static int PciInternalMatch(const DRIVER_MATCH* DriverMatch, const PCI_INFO* PciInfo);
static void PciFillFunctionInfo(U8 Bus, U8 Device, U8 Function, PCI_INFO* PciInfo);
static void PciDecodeBARs(const PCI_INFO* PciInfo, PCI_DEVICE* PciDevice);

/***************************************************************************/
// Registered PCI drivers

#define PCI_MAX_REGISTERED_DRIVERS 32

static LPPCI_DRIVER PciDriverTable[PCI_MAX_REGISTERED_DRIVERS];
static U32 PciDriverCount = 0;

/***************************************************************************/
// Low-level config space access (assumes port I/O helpers exist)

U32 PCI_Read32(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Address = PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset);
    OutPortLong(PCI_CONFIG_ADDRESS_PORT, Address);
    return InPortLong(PCI_CONFIG_DATA_PORT);
}

/***************************************************************************/

void PCI_Write32(U8 Bus, U8 Device, U8 Function, U16 Offset, U32 Value) {
    U32 Address = PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset);
    OutPortLong(PCI_CONFIG_ADDRESS_PORT, Address);
    OutPortLong(PCI_CONFIG_DATA_PORT, Value);
}

/***************************************************************************/

U16 PCI_Read16(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    return (U16)((Value32 >> ((Offset & 2U) * 8U)) & 0xFFFFU);
}

/***************************************************************************/

U8 PCI_Read8(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    return (U8)((Value32 >> ((Offset & 3U) * 8U)) & 0xFFU);
}

/***************************************************************************/

void PCI_Write16(U8 Bus, U8 Device, U8 Function, U16 Offset, U16 Value) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    U32 Shift = (Offset & 2U) * 8U;
    Value32 &= ~(0xFFFFU << Shift);
    Value32 |= ((U32)Value) << Shift;
    PCI_Write32(Bus, Device, Function, (U16)(Offset & ~3U), Value32);
}

/***************************************************************************/

void PCI_Write8(U8 Bus, U8 Device, U8 Function, U16 Offset, U8 Value) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    U32 Shift = (Offset & 3U) * 8U;
    Value32 &= ~(0xFFU << Shift);
    Value32 |= ((U32)Value) << Shift;
    PCI_Write32(Bus, Device, Function, (U16)(Offset & ~3U), Value32);
}

/***************************************************************************/
/* Command helpers                                                          */

U16 PCI_EnableBusMaster(U8 Bus, U8 Device, U8 Function, int Enable) {
    U16 Command = PCI_Read16(Bus, Device, Function, PCI_CFG_COMMAND);
    U16 Previous = Command;
    if (Enable) {
        Command |= PCI_CMD_BUSMASTER | PCI_CMD_MEM;
    } else {
        Command &= (U16)~PCI_CMD_BUSMASTER;
    }
    PCI_Write16(Bus, Device, Function, PCI_CFG_COMMAND, Command);
    return Previous;
}

/***************************************************************************/
/* BAR helpers                                                              */

static U32 PciReadBAR(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U16 Offset = (U16)(PCI_CFG_BAR0 + (BarIndex * 4));
    return PCI_Read32(Bus, Device, Function, Offset);
}

/***************************************************************************/

U32 PCI_GetBARBase(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U32 Bar = PciReadBAR(Bus, Device, Function, BarIndex);
    if (PCI_BAR_IS_IO(Bar)) {
        return (Bar & PCI_BAR_IO_MASK);
    } else {
        /* Memory BAR (treat 64-bit as returning low part for now) */
        return (Bar & PCI_BAR_MEM_MASK);
    }
}

/***************************************************************************/

U32 PCI_GetBARSize(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U16 Offset = (U16)(PCI_CFG_BAR0 + (BarIndex * 4));
    U32 Original = PCI_Read32(Bus, Device, Function, Offset);
    U32 Size = 0;

    /* Write all-ones to determine size mask per PCI spec */
    PCI_Write32(Bus, Device, Function, Offset, 0xFFFFFFFFU);
    U32 Probed = PCI_Read32(Bus, Device, Function, Offset);

    /* Restore original value */
    PCI_Write32(Bus, Device, Function, Offset, Original);

    if (PCI_BAR_IS_IO(Original)) {
        U32 Mask = Probed & PCI_BAR_IO_MASK;
        if (Mask != 0) Size = (~Mask) + 1U;
    } else {
        /* Memory BAR, may be 64-bit */
        U32 Type = (Original >> 1) & 0x3U;
        U32 Mask = Probed & PCI_BAR_MEM_MASK;
        if (Type == 0x2U) {
            /* 64-bit BAR: also probe high dword */
            U16 OffsetHigh = (U16)(Offset + 4);
            U32 OriginalHigh = PCI_Read32(Bus, Device, Function, OffsetHigh);
            PCI_Write32(Bus, Device, Function, OffsetHigh, 0xFFFFFFFFU);
            U32 ProbedHigh = PCI_Read32(Bus, Device, Function, OffsetHigh);
            PCI_Write32(Bus, Device, Function, OffsetHigh, OriginalHigh);

            /* We return the low 32-bit span as size for now */
            if ((Mask | ProbedHigh) != 0) {
                if (Mask != 0) Size = (~Mask) + 1U;
            }
        } else {
            if (Mask != 0) Size = (~Mask) + 1U;
        }
    }

    return Size;
}

/***************************************************************************/
/* Capabilities                                                             */

U8 PCI_FindCapability(U8 Bus, U8 Device, U8 Function, U8 CapabilityId) {
    U16 Status = PCI_Read16(Bus, Device, Function, PCI_CFG_STATUS);
    if ((Status & 0x10U) == 0) return 0; /* no cap list */

    U8 Pointer = PCI_Read8(Bus, Device, Function, PCI_CFG_CAP_PTR) & 0xFCU;

    /* Sanity iteration bound */
    for (U32 Index = 0; Index < 48 && Pointer >= 0x40; Index++) {
        U8 Id = PCI_Read8(Bus, Device, Function, (U16)(Pointer + 0));
        U8 Next = PCI_Read8(Bus, Device, Function, (U16)(Pointer + 1)) & 0xFCU;
        if (Id == CapabilityId) return Pointer;
        if (Next == 0 || Next == Pointer) break;
        Pointer = Next;
    }
    return 0;
}

/***************************************************************************/
/* Driver registration                                                      */

void PCI_RegisterDriver(LPPCI_DRIVER Driver) {
    if (Driver == NULL) return;
    if (PciDriverCount >= PCI_MAX_REGISTERED_DRIVERS) return;
    PciDriverTable[PciDriverCount++] = Driver;
    KernelLogText(LOG_DEBUG, TEXT("[PCI] Registered driver %s"), Driver->Product);
}

/***************************************************************************/
/* Scan & bind                                                              */

void PCI_ScanBus(void) {
    /* Use 32-bit counters to avoid wrap when PCI_MAX_* == 256 */
    U32 Bus, Device, Function;

    KernelLogText(LOG_DEBUG, TEXT("[PCI] Scanning bus"));

    for (Bus = 0; Bus < PCI_MAX_BUS; Bus++) {
        for (Device = 0; Device < PCI_MAX_DEV; Device++) {
            /* Check presence on function 0 */
            U16 VendorFunction0 = PCI_Read16((U8)Bus, (U8)Device, 0, PCI_CFG_VENDOR_ID);
            if (VendorFunction0 == 0xFFFFU) continue;

            U8 HeaderType = PCI_Read8((U8)Bus, (U8)Device, 0, PCI_CFG_HEADER_TYPE);
            U8 IsMultiFunction = (HeaderType & PCI_HEADER_MULTI_FN) ? 1 : 0;
            U8 MaxFunction = IsMultiFunction ? (PCI_MAX_FUNC - 1) : 0;

            for (Function = 0; Function <= (U32)MaxFunction; Function++) {
                U16 VendorId = PCI_Read16((U8)Bus, (U8)Device, (U8)Function, PCI_CFG_VENDOR_ID);
                if (VendorId == 0xFFFFU) continue;

                PCI_INFO PciInfo;
                PCI_DEVICE PciDevice;
                U32 DriverIndex;

                PciFillFunctionInfo((U8)Bus, (U8)Device, (U8)Function, &PciInfo);
                KernelLogText(
                    LOG_DEBUG, TEXT("[PCI] Found %X:%X.%u VID=%X DID=%X"), (U32)Bus, (U32)Device, (U32)Function,
                    (U32)PciInfo.VendorID, (U32)PciInfo.DeviceID);

                MemorySet(&PciDevice, 0, sizeof(PCI_DEVICE));
                PciDevice.ID = ID_PCIDEVICE;
                PciDevice.References = 1;
                PciDevice.Driver = NULL;
                PciDevice.Info = PciInfo;
                PciDecodeBARs(&PciInfo, &PciDevice);

                for (DriverIndex = 0; DriverIndex < PciDriverCount; DriverIndex++) {
                    LPPCI_DRIVER PciDriver = PciDriverTable[DriverIndex];

                    for (U32 MatchIndex = 0; MatchIndex < PciDriver->MatchCount; MatchIndex++) {
                        const DRIVER_MATCH* DriverMatch = &PciDriver->Matches[MatchIndex];

                        if (PciInternalMatch(DriverMatch, &PciInfo)) {
                            if (PciDriver->Command) {
                                KernelLogText(
                                    LOG_DEBUG, TEXT("[PCI] %s matches %X:%X.%u"), PciDriver->Product, (U32)Bus,
                                    (U32)Device, (U32)Function);

                                U32 Result = PciDriver->Command(DF_PROBE, (U32)(LPVOID)&PciInfo);
                                if (Result == DF_ERROR_SUCCESS) {
                                    PciDevice.Driver = (LPDRIVER)PciDriver;
                                    PciDriver->Command(DF_LOAD, 0);
                                    if (PciDriver->Attach) {
                                        LPPCI_DEVICE NewDev = PciDriver->Attach(&PciDevice);
                                        if (NewDev) {
                                            ListAddItem(Kernel.PCIDevice, NewDev);
                                            KernelLogText(
                                                LOG_DEBUG, TEXT("[PCI] Attached %s to %X:%X.%u"), PciDriver->Product,
                                                (U32)Bus, (U32)Device, (U32)Function);
                                            goto NextFunction;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            NextFunction:
                (void)0;
            }
        }
    }

    KernelLogText(LOG_DEBUG, TEXT("[PCI] Bus scan complete"));
}

/***************************************************************************/
/* Internals                                                                */

static int PciInternalMatch(const DRIVER_MATCH* DriverMatch, const PCI_INFO* PciInfo) {
    if (DriverMatch == NULL || PciInfo == NULL) return 0;

    if (DriverMatch->VendorID != PCI_ANY_ID && DriverMatch->VendorID != PciInfo->VendorID) return 0;
    if (DriverMatch->DeviceID != PCI_ANY_ID && DriverMatch->DeviceID != PciInfo->DeviceID) return 0;
    if (DriverMatch->BaseClass != PCI_ANY_CLASS && DriverMatch->BaseClass != PciInfo->BaseClass) return 0;
    if (DriverMatch->SubClass != PCI_ANY_CLASS && DriverMatch->SubClass != PciInfo->SubClass) return 0;
    if (DriverMatch->ProgIF != PCI_ANY_CLASS && DriverMatch->ProgIF != PciInfo->ProgIF) return 0;

    return 1;
}

/***************************************************************************/

static void PciFillFunctionInfo(U8 Bus, U8 Device, U8 Function, PCI_INFO* PciInfo) {
    U32 Index;

    PciInfo->Bus = Bus;
    PciInfo->Dev = Device;
    PciInfo->Func = Function;

    PciInfo->VendorID = PCI_Read16(Bus, Device, Function, PCI_CFG_VENDOR_ID);
    PciInfo->DeviceID = PCI_Read16(Bus, Device, Function, PCI_CFG_DEVICE_ID);

    PciInfo->BaseClass = PCI_Read8(Bus, Device, Function, PCI_CFG_BASECLASS);
    PciInfo->SubClass = PCI_Read8(Bus, Device, Function, PCI_CFG_SUBCLASS);
    PciInfo->ProgIF = PCI_Read8(Bus, Device, Function, PCI_CFG_PROG_IF);
    PciInfo->Revision = PCI_Read8(Bus, Device, Function, PCI_CFG_REVISION);

    for (Index = 0; Index < 6; Index++) {
        PciInfo->BAR[Index] = PCI_Read32(Bus, Device, Function, (U16)(PCI_CFG_BAR0 + Index * 4));
    }

    PciInfo->IRQLine = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_LINE);
    PciInfo->IRQLegacyPin = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_PIN);
}

/***************************************************************************/

static void PciDecodeBARs(const PCI_INFO* PciInfo, PCI_DEVICE* PciDevice) {
    U32 Index;
    for (Index = 0; Index < 6; Index++) {
        U32 BarValue = PciInfo->BAR[Index];
        if (PCI_BAR_IS_IO(BarValue)) {
            PciDevice->BARPhys[Index] = (BarValue & PCI_BAR_IO_MASK);
        } else {
            PciDevice->BARPhys[Index] = (BarValue & PCI_BAR_MEM_MASK);
        }
        PciDevice->BARMapped[Index] = NULL;
    }
}

/***************************************************************************/
