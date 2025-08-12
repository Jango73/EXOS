
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

    Intel E1000 (82540EM) Network Driver
    Minimal polling implementation using generic NETWORK DF_* API

\***************************************************************************/

#include "../include/E1000.h"

#include "../include/Base.h"
#include "../include/Driver.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Network.h"
#include "../include/PCI.h"
#include "../include/String.h"
#include "../include/User.h"
#include "../include/VMM.h"

/*
    RX & TX Descriptor Rings (E1000) - Example with 128 entries each
    -----------------------------------------------------------------
    Both rings are arrays of fixed-size descriptors (16 bytes), aligned and DMA-visible.
    The NIC and driver use RDH/RDT (RX) or TDH/TDT (TX) to coordinate ownership.

    =================================================================
    RECEIVE RING (RX) - hardware writes, driver reads
    =================================================================

        +--------------------------------------------------+
        |                                                  |
        v                                                  |
    +---------+    +---------+    +---------+    +---------+
    | Desc 0  | -> | Desc 1  | -> | Desc 2  | -> |  ...     |
    +---------+    +---------+    +---------+    +---------+
       ^                                ^
       |                                |
    RDH (Head)                      RDT (Tail)

    - RDH (Receive Descriptor Head):
        * Maintained by NIC.
        * Points to next descriptor NIC will fill with a received frame.
    - RDT (Receive Descriptor Tail):
        * Maintained by driver.
        * Points to last descriptor available to NIC.
        * Driver advances after processing a descriptor.

    Flow:
        1. NIC writes packet into RDH's buffer, sets DD (Descriptor Done).
        2. Driver polls/IRQ, processes data, clears DD.
        3. Driver advances RDT to give descriptor back to NIC.
        4. Wraps around modulo RX_DESC_COUNT.

    If RDH == RDT:
        Ring is FULL → NIC drops incoming packets.

    =================================================================
    TRANSMIT RING (TX) - driver writes, hardware reads
    =================================================================

        +--------------------------------------------------+
        |                                                  |
        v                                                  |
    +---------+    +---------+    +---------+    +---------+
    | Desc 0  | -> | Desc 1  | -> | Desc 2  | -> |  ...     |
    +---------+    +---------+    +---------+    +---------+
       ^                                ^
       |                                |
    TDH (Head)                      TDT (Tail)

    - TDH (Transmit Descriptor Head):
        * Maintained by NIC.
        * Points to next descriptor NIC will send.
    - TDT (Transmit Descriptor Tail):
        * Maintained by driver.
        * Points to next free descriptor for the driver to fill.
        * Driver advances after writing a packet.

    Flow:
        1. Driver writes packet buffer addr/len into TDT's descriptor.
        2. Driver sets CMD bits (EOP, IFCS, RS).
        3. Driver advances TDT to hand descriptor to NIC.
        4. NIC sends packet, sets DD in status.
        5. Driver checks DD to reclaim descriptor.

    If (TDT + 1) % TX_DESC_COUNT == TDH:
        Ring is FULL → driver must wait before sending more.
*/

/****************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

static U32 E1000Commands(U32 Function, U32 Param);

/****************************************************************/
// MMIO helpers

#define E1000_ReadReg32(Base, Off) (*(volatile U32 *)((U8 *)(Base) + (Off)))
#define E1000_WriteReg32(Base, Off, Val) (*(volatile U32 *)((U8 *)(Base) + (Off)) = (U32)(Val))

/****************************************************************/
// RX status bits (legacy)

#define E1000_RX_STA_DD 0x01
#define E1000_RX_STA_EOP 0x02

/****************************************************************/
// Small busy wait

static void E1000_Delay(U32 Iterations) {
    volatile U32 Index;
    for (Index = 0; Index < Iterations; Index++) {
        asm volatile("nop");
    }
}

/****************************************************************/
// Device structure

typedef struct tag_E1000DEVICE {
    PCI_DEVICE_FIELDS

    // MMIO mapping
    LINEAR MmioBase;
    U32 MmioSize;

    // MAC address
    U8 Mac[6];

    // RX ring
    PHYSICAL RxRingPhysical;
    LINEAR RxRingLinear;
    U32 RxRingCount;
    U32 RxHead;
    U32 RxTail;

    // TX ring
    PHYSICAL TxRingPhysical;
    LINEAR TxRingLinear;
    U32 TxRingCount;
    U32 TxHead;
    U32 TxTail;

    // RX buffers
    PHYSICAL RxBufPhysical[E1000_RX_DESC_COUNT];
    LINEAR RxBufLinear[E1000_RX_DESC_COUNT];

    // TX buffers
    PHYSICAL TxBufPhysical[E1000_TX_DESC_COUNT];
    LINEAR TxBufLinear[E1000_TX_DESC_COUNT];

    // RX callback (set via DF_NT_SETRXCB)
    NT_RXCB RxCallback;
} E1000DEVICE, *LPE1000DEVICE;

/****************************************************************/
// Globals and PCI match table

static DRIVER_MATCH E1000_MatchTable[] = {E1000_MATCH_DEFAULT};

static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDev);

PCI_DRIVER E1000Driver = {
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "E1000 (82540EM)",
    .Command = E1000Commands,
    .Matches = E1000_MatchTable,
    .MatchCount = sizeof(E1000_MatchTable) / sizeof(E1000_MatchTable[0]),
    .Attach = E1000_Attach};

static LPE1000DEVICE NewE1000Device(LPPCI_DEVICE PciDevice) {
    KernelLogText(
        LOG_DEBUG, TEXT("[E1000] New device %02X:%02X.%u"), PciDevice->Info.Bus, PciDevice->Info.Dev,
        PciDevice->Info.Func);
    LPE1000DEVICE Device = (LPE1000DEVICE)KernelMemAlloc(sizeof(E1000DEVICE));
    if (Device == NULL) return NULL;

    MemorySet(Device, 0, sizeof(E1000DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    Device->Next = NULL;
    Device->Prev = NULL;
    Device->References = 1;
    Device->Driver = PciDevice->Driver;

    U32 Bar0Phys = Device->BARPhys[0];
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    if (Bar0Phys == 0 || Bar0Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000] Invalid BAR0"));
        KernelMemFree(Device);
        return NULL;
    }

    Device->MmioBase = MmMapIo(Bar0Phys, Bar0Size);
    Device->MmioSize = Bar0Size;
    if (Device->MmioBase == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000] MmMapIo failed"));
        KernelMemFree(Device);
        return NULL;
    }
    KernelLogText(LOG_DEBUG, TEXT("[E1000] MMIO mapped at %X size %X"), Device->MmioBase, Device->MmioSize);

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 1);

    if (!E1000_Reset(Device)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000] Reset failed"));
        KernelMemFree(Device);
        return NULL;
    }
    KernelLogText(LOG_DEBUG, TEXT("[E1000] Reset complete"));
    E1000_ReadMac(Device);

    if (!E1000_SetupRx(Device)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000] RX setup failed"));
        KernelMemFree(Device);
        return NULL;
    }
    KernelLogText(LOG_DEBUG, TEXT("[E1000] RX setup complete"));
    if (!E1000_SetupTx(Device)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000] TX setup failed"));
        KernelMemFree(Device);
        return NULL;
    }
    KernelLogText(LOG_DEBUG, TEXT("[E1000] TX setup complete"));

    KernelLogText(
        LOG_VERBOSE, TEXT("[E1000] Attached %02X:%02X.%u MMIO=%X size=%X MAC=%02X:%02X:%02X:%02X:%02X:%02X"),
        Device->Info.Bus, Device->Info.Dev, Device->Info.Func, Device->MmioBase, Device->MmioSize, Device->Mac[0],
        Device->Mac[1], Device->Mac[2], Device->Mac[3], Device->Mac[4], Device->Mac[5]);

    return Device;
}

static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDev) { return (LPPCI_DEVICE)NewE1000Device(PciDev); }

/****************************************************************/
// EEPROM read and MAC

static U16 E1000_EepromReadWord(LPE1000DEVICE Device, U32 Address) {
    U32 Value = 0;
    U32 Count = 0;

    E1000_WriteReg32(Device->MmioBase, E1000_REG_EERD, ((Address & 0xFF) << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    while (Count < 100000) {
        Value = E1000_ReadReg32(Device->MmioBase, E1000_REG_EERD);
        if (Value & E1000_EERD_DONE) break;
        Count++;
    }

    return (U16)((Value >> E1000_EERD_DATA_SHIFT) & 0xFFFF);
}

static void E1000_ReadMac(LPE1000DEVICE Device) {
    U32 low = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAL0);
    U32 high = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAH0);

    if (high & (1u << 31)) {
        // RAL/RAH déjà valides
        Device->Mac[0] = (low >> 0) & 0xFF;
        Device->Mac[1] = (low >> 8) & 0xFF;
        Device->Mac[2] = (low >> 16) & 0xFF;
        Device->Mac[3] = (low >> 24) & 0xFF;
        Device->Mac[4] = (high >> 0) & 0xFF;
        Device->Mac[5] = (high >> 8) & 0xFF;
        return;
    }

    // Fallback: EEPROM (permanent MAC), puis on programme RAL/RAH
    U16 w0 = E1000_EepromReadWord(Device, 0);
    U16 w1 = E1000_EepromReadWord(Device, 1);
    U16 w2 = E1000_EepromReadWord(Device, 2);

    Device->Mac[0] = (U8)(w0 & 0xFF);
    Device->Mac[1] = (U8)(w0 >> 8);
    Device->Mac[2] = (U8)(w1 & 0xFF);
    Device->Mac[3] = (U8)(w1 >> 8);
    Device->Mac[4] = (U8)(w2 & 0xFF);
    Device->Mac[5] = (U8)(w2 >> 8);

    low = (Device->Mac[0] << 0) | (Device->Mac[1] << 8) | (Device->Mac[2] << 16) | (Device->Mac[3] << 24);
    high = (Device->Mac[4] << 0) | (Device->Mac[5] << 8) | (1u << 31);  // AV=1
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, low);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, high);
}

/****************************************************************/
// Core HW ops

static BOOL E1000_Reset(LPE1000DEVICE Device) {
    KernelLogText(LOG_DEBUG, TEXT("[E1000_Reset] Begin"));
    U32 Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl | E1000_CTRL_RST);

    U32 Count = 0;
    while (Count < 100000) {
        Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
        if ((Ctrl & E1000_CTRL_RST) == 0) break;
        Count++;
    }

    Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
    Ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl);

    // Disable interrupts for polling path
    E1000_WriteReg32(Device->MmioBase, E1000_REG_IMC, 0xFFFFFFFF);

    KernelLogText(LOG_DEBUG, TEXT("[E1000_Reset] Done"));
    return TRUE;
}

/****************************************************************/
// RX/TX rings setup

static BOOL E1000_SetupRx(LPE1000DEVICE Device) {
    KernelLogText(LOG_DEBUG, TEXT("[E1000_SetupRx] Begin"));
    U32 Index;

    Device->RxRingCount = E1000_RX_DESC_COUNT;

    Device->RxRingPhysical = AllocPhysicalPage();
    if (Device->RxRingPhysical == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx ring phys alloc failed"));
        return FALSE;
    }
    Device->RxRingLinear =
        VirtualAlloc(0, Device->RxRingPhysical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->RxRingLinear == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx ring map failed"));
        return FALSE;
    }

    MemorySet((LPVOID)Device->RxRingLinear, 0, PAGE_SIZE);

    for (Index = 0; Index < Device->RxRingCount; Index++) {
        Device->RxBufPhysical[Index] = AllocPhysicalPage();
        if (Device->RxBufPhysical[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx buf phys alloc failed"));
            return FALSE;
        }
        Device->RxBufLinear[Index] = VirtualAlloc(
            0, Device->RxBufPhysical[Index], E1000_RX_BUF_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
        if (Device->RxBufLinear[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx buf map failed"));
            return FALSE;
        }
    }

    {
        LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingLinear;

        for (Index = 0; Index < Device->RxRingCount; Index++) {
            Ring[Index].BufferAddrLow = (U32)(Device->RxBufPhysical[Index] & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Status = 0;
            Ring[Index].Length = 0;
            Ring[Index].Errors = 0;
            Ring[Index].Special = 0;
        }

        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAL, (U32)(Device->RxRingPhysical & 0xFFFFFFFF));
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAH, 0);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDLEN, Device->RxRingCount * sizeof(E1000_RXDESC));
        Device->RxHead = 0;
        Device->RxTail = Device->RxRingCount - 1;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDH, Device->RxHead);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, Device->RxTail);

        U32 Rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RCTL, Rctl);
    }

    KernelLogText(LOG_DEBUG, TEXT("[E1000_SetupRx] Done"));
    return TRUE;
}

static BOOL E1000_SetupTx(LPE1000DEVICE Device) {
    KernelLogText(LOG_DEBUG, TEXT("[E1000_SetupTx] Begin"));
    U32 Index;

    Device->TxRingCount = E1000_TX_DESC_COUNT;

    Device->TxRingPhysical = AllocPhysicalPage();
    if (Device->TxRingPhysical == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx ring phys alloc failed"));
        return FALSE;
    }
    Device->TxRingLinear =
        VirtualAlloc(0, Device->TxRingPhysical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->TxRingLinear == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx ring map failed"));
        return FALSE;
    }

    MemorySet((LPVOID)Device->TxRingLinear, 0, PAGE_SIZE);

    for (Index = 0; Index < Device->TxRingCount; Index++) {
        Device->TxBufPhysical[Index] = AllocPhysicalPage();
        if (Device->TxBufPhysical[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx buf phys alloc failed"));
            return FALSE;
        }
        Device->TxBufLinear[Index] = VirtualAlloc(
            0, Device->TxBufPhysical[Index], E1000_RX_BUF_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
        if (Device->TxBufLinear[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx buf map failed"));
            return FALSE;
        }
    }

    {
        LPE1000_TXDESC Ring = (LPE1000_TXDESC)Device->TxRingLinear;

        for (Index = 0; Index < Device->TxRingCount; Index++) {
            Ring[Index].BufferAddrLow = (U32)(Device->TxBufPhysical[Index] & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Length = 0;
            Ring[Index].CSO = 0;
            Ring[Index].CMD = 0;
            Ring[Index].STA = 0;
            Ring[Index].CSS = 0;
            Ring[Index].Special = 0;
        }

        E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAL, (U32)(Device->TxRingPhysical & 0xFFFFFFFF));
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAH, 0);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TDLEN, Device->TxRingCount * sizeof(E1000_TXDESC));
        Device->TxHead = 0;
        Device->TxTail = 0;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TDH, Device->TxHead);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, Device->TxTail);

        U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x0F << E1000_TCTL_CT_SHIFT) | (0x40 << E1000_TCTL_COLD_SHIFT);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TCTL, Tctl);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TIPG, 0x0060200A);
    }

    KernelLogText(LOG_DEBUG, TEXT("[E1000_SetupTx] Done"));
    return TRUE;
}

/****************************************************************/
// RX/TX operations
static U32 E1000_TxSend(LPE1000DEVICE Device, const U8 *Data, U32 Length) {
    if (Length == 0 || Length > E1000_RX_BUF_SIZE) return DF_ERROR_BADPARAM;

    KernelLogText(LOG_DEBUG, TEXT("[E1000_TxSend] len=%u"), Length);

    U32 Index = Device->TxTail;
    LPE1000_TXDESC Ring = (LPE1000_TXDESC)Device->TxRingLinear;

    // Copy into pre-allocated TX buffer
    MemoryCopy((LPVOID)Device->TxBufLinear[Index], (LPVOID)Data, Length);

    Ring[Index].Length = (U16)Length;
    Ring[Index].CMD = (E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    Ring[Index].STA = 0;

    // Advance tail
    U32 NewTail = (Index + 1) % Device->TxRingCount;
    Device->TxTail = NewTail;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, NewTail);

    // Simple spin for DD
    U32 Wait = 0;
    while (((Ring[Index].STA & E1000_TX_STA_DD) == 0) && (Wait++ < 100000)) {
    }

    KernelLogText(LOG_DEBUG, TEXT("[E1000_TxSend] sent index=%u"), Index);
    return DF_ERROR_SUCCESS;
}

static U32 E1000_RxPoll(LPE1000DEVICE Device) {
    KernelLogText(LOG_DEBUG, TEXT("[E1000_RxPoll] Begin"));
    LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingLinear;
    U32 Count = 0;

    while (1) {
        U32 NextIndex = (Device->RxHead) % Device->RxRingCount;
        U8 Status = Ring[NextIndex].Status;

        if ((Status & E1000_RX_STA_DD) == 0) break;

        if ((Status & E1000_RX_STA_EOP) != 0) {
            U16 Length = Ring[NextIndex].Length;
            const U8 *Frame = (const U8 *)Device->RxBufLinear[NextIndex];
            if (Device->RxCallback) {
                Device->RxCallback(Frame, (U32)Length);
            }
        }

        Ring[NextIndex].Status = 0;
        Device->RxHead = (NextIndex + 1) % Device->RxRingCount;

        U32 NewTail = (Device->RxTail + 1) % Device->RxRingCount;
        Device->RxTail = NewTail;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, NewTail);

        Count++;
        if (Count > Device->RxRingCount) break;
    }

    KernelLogText(LOG_DEBUG, TEXT("[E1000_RxPoll] processed=%u"), Count);
    return DF_ERROR_SUCCESS;
}

/****************************************************************/
// PCI-level helpers (per-function)

static U32 E1000_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo->VendorID != E1000_VENDOR_INTEL) return DF_ERROR_NOTIMPL;
    if (PciInfo->DeviceID != E1000_DEVICE_82540EM) return DF_ERROR_NOTIMPL;
    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) return DF_ERROR_NOTIMPL;
    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) return DF_ERROR_NOTIMPL;
    return DF_ERROR_SUCCESS;
}

// Network DF_* helpers (per-function)

static U32 E1000_OnReset(const NETWORKRESET *Reset) {
    if (Reset == NULL || Reset->Device == NULL) return DF_ERROR_BADPARAM;
    return E1000_Reset((LPE1000DEVICE)Reset->Device) ? DF_ERROR_SUCCESS : DF_ERROR_UNEXPECT;
}

static U32 E1000_OnGetInfo(const NETWORKGETINFO *Get) {
    if (Get == NULL || Get->Device == NULL || Get->Info == NULL) return DF_ERROR_BADPARAM;
    LPE1000DEVICE Device = (LPE1000DEVICE)Get->Device;
    U32 Status = E1000_ReadReg32(Device->MmioBase, E1000_REG_STATUS);

    Get->Info->MAC[0] = Device->Mac[0];
    Get->Info->MAC[1] = Device->Mac[1];
    Get->Info->MAC[2] = Device->Mac[2];
    Get->Info->MAC[3] = Device->Mac[3];
    Get->Info->MAC[4] = Device->Mac[4];
    Get->Info->MAC[5] = Device->Mac[5];

    Get->Info->LinkUp = (Status & E1000_STATUS_LU) ? 1 : 0;
    Get->Info->SpeedMbps = 1000;
    Get->Info->DuplexFull = (Status & E1000_STATUS_FD) ? 1 : 0;
    Get->Info->MTU = 1500;

    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnSetRxCb(const NETWORKSETRXCB *Set) {
    if (Set == NULL || Set->Device == NULL) return DF_ERROR_BADPARAM;
    LPE1000DEVICE Device = (LPE1000DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnSend(const NETWORKSEND *Send) {
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) return DF_ERROR_BADPARAM;
    return E1000_TxSend((LPE1000DEVICE)Send->Device, Send->Data, Send->Length);
}

static U32 E1000_OnPoll(const NETWORKPOLL *Poll) {
    if (Poll == NULL || Poll->Device == NULL) return DF_ERROR_BADPARAM;
    return E1000_RxPoll((LPE1000DEVICE)Poll->Device);
}

/****************************************************************/
// Driver meta helpers

static U32 E1000_OnLoad(void) { return DF_ERROR_SUCCESS; }

static U32 E1000_OnUnload(void) { return DF_ERROR_SUCCESS; }

static U32 E1000_OnGetVersion(void) { return MAKE_VERSION(VER_MAJOR, VER_MINOR); }

static U32 E1000_OnGetCaps(void) { return 0; }

static U32 E1000_OnGetLastFunc(void) { return DF_NT_POLL; }

/****************************************************************/
// Driver entry

static U32 E1000Commands(U32 Function, U32 Param) {
    switch (Function) {
        case DF_LOAD:
            return E1000_OnLoad();
        case DF_UNLOAD:
            return E1000_OnUnload();
        case DF_GETVERSION:
            return E1000_OnGetVersion();
        case DF_GETCAPS:
            return E1000_OnGetCaps();
        case DF_GETLASTFUNC:
            return E1000_OnGetLastFunc();

        // PCI binding
        case DF_PROBE:
            return E1000_OnProbe((const PCI_INFO *)(LPVOID)Param);

        // Network DF_* API
        case DF_NT_RESET:
            return E1000_OnReset((const NETWORKRESET *)(LPVOID)Param);
        case DF_NT_GETINFO:
            return E1000_OnGetInfo((const NETWORKGETINFO *)(LPVOID)Param);
        case DF_NT_SETRXCB:
            return E1000_OnSetRxCb((const NETWORKSETRXCB *)(LPVOID)Param);
        case DF_NT_SEND:
            return E1000_OnSend((const NETWORKSEND *)(LPVOID)Param);
        case DF_NT_POLL:
            return E1000_OnPoll((const NETWORKPOLL *)(LPVOID)Param);
    }

    return DF_ERROR_NOTIMPL;
}
