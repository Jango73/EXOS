
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

    Intel E1000 (82540EM) Network Driver
    Minimal polling implementation using generic NETWORK DF_* API

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Driver.h"
#include "../include/E1000.h"
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

#define E1000_ReadReg32(Base, Off)        (*(volatile U32 *)((U8 *)(Base) + (Off)))
#define E1000_WriteReg32(Base, Off, Val)  (*(volatile U32 *)((U8 *)(Base) + (Off)) = (U32)(Val))

/****************************************************************/
// RX status bits (legacy)

#define E1000_RX_STA_DD   0x01
#define E1000_RX_STA_EOP  0x02

/****************************************************************/
// Small busy wait

static void E1000_Delay(U32 Iterations) {
    volatile U32 Index;
    for (Index = 0; Index < Iterations; Index++) { asm volatile ("nop"); }
}

/****************************************************************/
// Driver context

typedef struct tag_E1000_CONTEXT {
    // MMIO mapping
    LINEAR MmioBase;
    U32    MmioSize;

    // PCI identity
    U8  Bus;
    U8  Device;
    U8  Function;

    // MAC address
    U8  Mac[6];

    // RX ring
    PHYSICAL RxRingPhysical;
    LINEAR   RxRingLinear;
    U32      RxRingCount;
    U32      RxHead;
    U32      RxTail;

    // TX ring
    PHYSICAL TxRingPhysical;
    LINEAR   TxRingLinear;
    U32      TxRingCount;
    U32      TxHead;
    U32      TxTail;

    // RX buffers
    PHYSICAL RxBufPhysical[E1000_RX_DESC_COUNT];
    LINEAR   RxBufLinear[E1000_RX_DESC_COUNT];

    // TX buffers
    PHYSICAL TxBufPhysical[E1000_TX_DESC_COUNT];
    LINEAR   TxBufLinear[E1000_TX_DESC_COUNT];

    // RX callback (set via DF_NT_SETRXCB)
    void (*RxCallback)(const U8 *Frame, U32 Length);
} E1000_CONTEXT, *LPE1000_CONTEXT;

/****************************************************************/
// Globals and PCI match table

static DRIVER_MATCH E1000_MatchTable[] = {
    E1000_MATCH_DEFAULT
};

PCI_DRIVER E1000Driver = {
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "E1000 (82540EM)",
    .Command = E1000Commands
    .Matches = E1000_MatchTable,
    .MatchCount = sizeof(E1000_MatchTable) / sizeof(E1000_MatchTable[0])
};

static E1000_CONTEXT *E1000_Singleton = NULL;

/****************************************************************/
// EEPROM read and MAC

static U16 E1000_EepromReadWord(LPE1000_CONTEXT Context, U32 Address) {
    U32 Value = 0;
    U32 Count = 0;

    E1000_WriteReg32(Context->MmioBase, E1000_REG_EERD,
        ((Address & 0xFF) << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    while (Count < 100000) {
        Value = E1000_ReadReg32(Context->MmioBase, E1000_REG_EERD);
        if (Value & E1000_EERD_DONE) break;
        Count++;
    }

    return (U16)((Value >> E1000_EERD_DATA_SHIFT) & 0xFFFF);
}

static void E1000_ReadMac(LPE1000_CONTEXT Ctx) {
    U32 low  = E1000_ReadReg32(Ctx->MmioBase, E1000_REG_RAL0);
    U32 high = E1000_ReadReg32(Ctx->MmioBase, E1000_REG_RAH0);

    if (high & (1u << 31)) {
        // RAL/RAH déjà valides
        Ctx->Mac[0] = (low >> 0)  & 0xFF;
        Ctx->Mac[1] = (low >> 8)  & 0xFF;
        Ctx->Mac[2] = (low >> 16) & 0xFF;
        Ctx->Mac[3] = (low >> 24) & 0xFF;
        Ctx->Mac[4] = (high >> 0) & 0xFF;
        Ctx->Mac[5] = (high >> 8) & 0xFF;
        return;
    }

    // Fallback: EEPROM (permanent MAC), puis on programme RAL/RAH
    U16 w0 = E1000_EepromReadWord(Ctx, 0);
    U16 w1 = E1000_EepromReadWord(Ctx, 1);
    U16 w2 = E1000_EepromReadWord(Ctx, 2);

    Ctx->Mac[0] = (U8)(w0 & 0xFF);
    Ctx->Mac[1] = (U8)(w0 >> 8);
    Ctx->Mac[2] = (U8)(w1 & 0xFF);
    Ctx->Mac[3] = (U8)(w1 >> 8);
    Ctx->Mac[4] = (U8)(w2 & 0xFF);
    Ctx->Mac[5] = (U8)(w2 >> 8);

    low  = (Ctx->Mac[0] << 0) | (Ctx->Mac[1] << 8) | (Ctx->Mac[2] << 16) | (Ctx->Mac[3] << 24);
    high = (Ctx->Mac[4] << 0) | (Ctx->Mac[5] << 8) | (1u << 31); // AV=1
    E1000_WriteReg32(Ctx->MmioBase, E1000_REG_RAL0, low);
    E1000_WriteReg32(Ctx->MmioBase, E1000_REG_RAH0, high);
}

/****************************************************************/
// Core HW ops

static BOOL E1000_Reset(LPE1000_CONTEXT Context) {
    U32 Ctrl = E1000_ReadReg32(Context->MmioBase, E1000_REG_CTRL);
    E1000_WriteReg32(Context->MmioBase, E1000_REG_CTRL, Ctrl | E1000_CTRL_RST);

    U32 Count = 0;
    while (Count < 100000) {
        Ctrl = E1000_ReadReg32(Context->MmioBase, E1000_REG_CTRL);
        if ((Ctrl & E1000_CTRL_RST) == 0) break;
        Count++;
    }

    Ctrl = E1000_ReadReg32(Context->MmioBase, E1000_REG_CTRL);
    Ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
    E1000_WriteReg32(Context->MmioBase, E1000_REG_CTRL, Ctrl);

    // Disable interrupts for polling path
    E1000_WriteReg32(Context->MmioBase, E1000_REG_IMC, 0xFFFFFFFF);

    return TRUE;
}

/****************************************************************/
// RX/TX rings setup

static BOOL E1000_SetupRx(LPE1000_CONTEXT Context) {
    U32 Index;

    Context->RxRingCount = E1000_RX_DESC_COUNT;

    Context->RxRingPhysical = AllocPhysicalPage();
    if (Context->RxRingPhysical == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx ring phys alloc failed"));
        return FALSE;
    }
    Context->RxRingLinear = VirtualAlloc(0, Context->RxRingPhysical, PAGE_SIZE,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Context->RxRingLinear == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx ring map failed"));
        return FALSE;
    }

    MemorySet((LPVOID)Context->RxRingLinear, 0, PAGE_SIZE);

    for (Index = 0; Index < Context->RxRingCount; Index++) {
        Context->RxBufPhysical[Index] = AllocPhysicalPage();
        if (Context->RxBufPhysical[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx buf phys alloc failed"));
            return FALSE;
        }
        Context->RxBufLinear[Index] = VirtualAlloc(0, Context->RxBufPhysical[Index], E1000_RX_BUF_SIZE,
            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
        if (Context->RxBufLinear[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupRx] Rx buf map failed"));
            return FALSE;
        }
    }

    {
        LPE1000_RXDESC Ring = (LPE1000_RXDESC)Context->RxRingLinear;

        for (Index = 0; Index < Context->RxRingCount; Index++) {
            Ring[Index].BufferAddrLow  = (U32)(Context->RxBufPhysical[Index] & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Status = 0;
            Ring[Index].Length = 0;
            Ring[Index].Errors = 0;
            Ring[Index].Special = 0;
        }

        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDBAL, (U32)(Context->RxRingPhysical & 0xFFFFFFFF));
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDBAH, 0);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDLEN, Context->RxRingCount * sizeof(E1000_RXDESC));
        Context->RxHead = 0;
        Context->RxTail = Context->RxRingCount - 1;
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDH, Context->RxHead);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDT, Context->RxTail);

        U32 Rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RCTL, Rctl);
    }

    return TRUE;
}

static BOOL E1000_SetupTx(LPE1000_CONTEXT Context) {
    U32 Index;

    Context->TxRingCount = E1000_TX_DESC_COUNT;

    Context->TxRingPhysical = AllocPhysicalPage();
    if (Context->TxRingPhysical == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx ring phys alloc failed"));
        return FALSE;
    }
    Context->TxRingLinear = VirtualAlloc(0, Context->TxRingPhysical, PAGE_SIZE,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Context->TxRingLinear == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx ring map failed"));
        return FALSE;
    }

    MemorySet((LPVOID)Context->TxRingLinear, 0, PAGE_SIZE);

    for (Index = 0; Index < Context->TxRingCount; Index++) {
        Context->TxBufPhysical[Index] = AllocPhysicalPage();
        if (Context->TxBufPhysical[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx buf phys alloc failed"));
            return FALSE;
        }
        Context->TxBufLinear[Index] = VirtualAlloc(0, Context->TxBufPhysical[Index], E1000_RX_BUF_SIZE,
            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
        if (Context->TxBufLinear[Index] == 0) {
            KernelLogText(LOG_ERROR, TEXT("[E1000_SetupTx] Tx buf map failed"));
            return FALSE;
        }
    }

    {
        LPE1000_TXDESC Ring = (LPE1000_TXDESC)Context->TxRingLinear;

        for (Index = 0; Index < Context->TxRingCount; Index++) {
            Ring[Index].BufferAddrLow  = (U32)(Context->TxBufPhysical[Index] & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Length = 0;
            Ring[Index].CSO = 0;
            Ring[Index].CMD = 0;
            Ring[Index].STA = 0;
            Ring[Index].CSS = 0;
            Ring[Index].Special = 0;
        }

        E1000_WriteReg32(Context->MmioBase, E1000_REG_TDBAL, (U32)(Context->TxRingPhysical & 0xFFFFFFFF));
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TDBAH, 0);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TDLEN, Context->TxRingCount * sizeof(E1000_TXDESC));
        Context->TxHead = 0;
        Context->TxTail = 0;
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TDH, Context->TxHead);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TDT, Context->TxTail);

        U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x0F << E1000_TCTL_CT_SHIFT) | (0x40 << E1000_TCTL_COLD_SHIFT);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TCTL, Tctl);
        E1000_WriteReg32(Context->MmioBase, E1000_REG_TIPG, 0x0060200A);
    }

    return TRUE;
}

/****************************************************************/
// RX/TX operations
static U32 E1000_TxSend(LPE1000_CONTEXT Context, const U8 *Data, U32 Length) {
    if (Length == 0 || Length > E1000_RX_BUF_SIZE) return DF_ERROR_BADPARAM;

    U32 Index = Context->TxTail;
    LPE1000_TXDESC Ring = (LPE1000_TXDESC)Context->TxRingLinear;

    // Copy into pre-allocated TX buffer
    MemoryCopy((LPVOID)Context->TxBufLinear[Index], (LPVOID)Data, Length);

    Ring[Index].Length = (U16)Length;
    Ring[Index].CMD = (E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    Ring[Index].STA = 0;

    // Advance tail
    U32 NewTail = (Index + 1) % Context->TxRingCount;
    Context->TxTail = NewTail;
    E1000_WriteReg32(Context->MmioBase, E1000_REG_TDT, NewTail);

    // Simple spin for DD
    U32 Wait = 0;
    while (((Ring[Index].STA & E1000_TX_STA_DD) == 0) && (Wait++ < 100000)) { }

    return DF_ERROR_SUCCESS;
}

static U32 E1000_RxPoll(LPE1000_CONTEXT Context) {
    LPE1000_RXDESC Ring = (LPE1000_RXDESC)Context->RxRingLinear;
    U32 Count = 0;

    while (1) {
        U32 NextIndex = (Context->RxHead) % Context->RxRingCount;
        U8 Status = Ring[NextIndex].Status;

        if ((Status & E1000_RX_STA_DD) == 0) break;

        if ((Status & E1000_RX_STA_EOP) != 0) {
            U16 Length = Ring[NextIndex].Length;
            const U8 *Frame = (const U8 *)Context->RxBufLinear[NextIndex];
            if (Context->RxCallback) {
                Context->RxCallback(Frame, (U32)Length);
            }
        }

        Ring[NextIndex].Status = 0;
        Context->RxHead = (NextIndex + 1) % Context->RxRingCount;

        U32 NewTail = (Context->RxTail + 1) % Context->RxRingCount;
        Context->RxTail = NewTail;
        E1000_WriteReg32(Context->MmioBase, E1000_REG_RDT, NewTail);

        Count++;
        if (Count > Context->RxRingCount) break;
    }

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

static U32 E1000_OnAttach(PCI_DEVICE *PciDevice) {
    E1000_CONTEXT *Context = (E1000_CONTEXT *)KernelMemAlloc(sizeof(E1000_CONTEXT));
    if (Context == NULL) return DF_ERROR_NOMEMORY;

    MemorySet(Context, 0, sizeof(E1000_CONTEXT));

    Context->Bus = PciDevice->Info.Bus;
    Context->Device = PciDevice->Info.Dev;
    Context->Function = PciDevice->Info.Func;

    // Map BAR0 (MMIO) as UC
    U32 Bar0Phys = PciDevice->BARPhys[0];
    U32 Bar0Size = PCI_GetBARSize(Context->Bus, Context->Device, Context->Function, 0);
    if (Bar0Phys == 0 || Bar0Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_OnAttach] Invalid BAR0"));
        KernelMemFree(Context);
        return DF_ERROR_UNEXPECT;
    }

    Context->MmioBase = MmMapIo(Bar0Phys, Bar0Size);
    Context->MmioSize = Bar0Size;
    if (Context->MmioBase == 0) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_OnAttach] MmMapIo failed"));
        KernelMemFree(Context);
        return DF_ERROR_UNEXPECT;
    }

    // Enable Bus Mastering
    PCI_EnableBusMaster(Context->Bus, Context->Device, Context->Function, 1);

    // Reset + MAC
    if (!E1000_Reset(Context)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_OnAttach] Reset failed"));
        KernelMemFree(Context);
        return DF_ERROR_UNEXPECT;
    }
    E1000_ReadMac(Context);

    // Rings
    if (!E1000_SetupRx(Context)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_OnAttach] RX setup failed"));
        KernelMemFree(Context);
        return DF_ERROR_UNEXPECT;
    }
    if (!E1000_SetupTx(Context)) {
        KernelLogText(LOG_ERROR, TEXT("[E1000_OnAttach] TX setup failed"));
        KernelMemFree(Context);
        return DF_ERROR_UNEXPECT;
    }

    PciDevice->DriverContext = Context;
    E1000_Singleton = Context;

    KernelLogText(LOG_VERBOSE, TEXT("[E1000] Attached %02X:%02X.%u MMIO=%X size=%X MAC=%02X:%02X:%02X:%02X:%02X:%02X"),
        Context->Bus, Context->Device, Context->Function,
        Context->MmioBase, Context->MmioSize,
        Context->Mac[0], Context->Mac[1], Context->Mac[2], Context->Mac[3], Context->Mac[4], Context->Mac[5]);

    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnDetach(PCI_DEVICE *PciDevice) {
    E1000_CONTEXT *Context = (E1000_CONTEXT *)PciDevice->DriverContext;
    if (Context == NULL) return DF_ERROR_SUCCESS;

    // TODO: free all RX/TX buffers and rings (if you want clean teardown)
    if (Context->MmioBase) {
        MmUnmapIo(Context->MmioBase, Context->MmioSize);
    }

    KernelMemFree(Context);
    PciDevice->DriverContext = NULL;
    if (E1000_Singleton == Context) E1000_Singleton = NULL;
    return DF_ERROR_SUCCESS;
}

/****************************************************************/
// Network DF_* helpers (per-function)

static U32 E1000_OnReset(void) {
    if (E1000_Singleton == NULL) return DF_ERROR_UNEXPECT;
    return E1000_Reset(E1000_Singleton) ? DF_ERROR_SUCCESS : DF_ERROR_UNEXPECT;
}

static U32 E1000_OnGetInfo(LPNETWORKINFO Info) {
    if (E1000_Singleton == NULL) return DF_ERROR_UNEXPECT;
    if (Info == NULL || Info->Header.Size < sizeof(NETWORKINFO)) return DF_ERROR_BADPARAM;

    E1000_CONTEXT *Context = (E1000_CONTEXT *)PciDevice->DriverContext;
    if (Context == NULL) return DF_ERROR_UNEXPECT;

    U32 Status = E1000_ReadReg32(E1000_Singleton->MmioBase, E1000_REG_STATUS);

    Info->MAC[0] = Context->Mac[0];
    Info->MAC[1] = Context->Mac[1];
    Info->MAC[2] = Context->Mac[2];
    Info->MAC[3] = Context->Mac[3];
    Info->MAC[4] = Context->Mac[4];
    Info->MAC[5] = Context->Mac[5];

    Info->LinkUp = (Status & E1000_STATUS_LU) ? 1 : 0;
    Info->SpeedMbps = 1000;
    Info->DuplexFull = (Status & E1000_STATUS_FD) ? 1 : 0;
    Info->MTU = 1500;

    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnSetRxCb(NT_RXCB Callback) {
    if (E1000_Singleton == NULL) return DF_ERROR_UNEXPECT;
    E1000_Singleton->RxCallback = Callback;
    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnSend(const NETWORKSEND* Send) {
    if (E1000_Singleton == NULL) return DF_ERROR_UNEXPECT;
    if (Send == NULL || Send->Data == NULL || Send->Length == 0) return DF_ERROR_BADPARAM;
    return E1000_TxSend(E1000_Singleton, Send->Data, Send->Length);
}

static U32 E1000_OnPoll(void) {
    if (E1000_Singleton == NULL) return DF_ERROR_UNEXPECT;
    return E1000_RxPoll(E1000_Singleton);
}

/****************************************************************/
// Driver meta helpers

static U32 E1000_OnLoad(void) {
    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnUnload(void) {
    return DF_ERROR_SUCCESS;
}

static U32 E1000_OnGetVersion(void) {
    return MAKE_VERSION(VER_MAJOR, VER_MINOR);
}

static U32 E1000_OnGetCaps(void) {
    return 0;
}

static U32 E1000_OnGetLastFunc(void) {
    return DF_NT_POLL;
}

/****************************************************************/
// Driver entry

static U32 E1000Commands(U32 Function, U32 Param) { switch (Function) {
        case DF_LOAD:        return E1000_OnLoad();
        case DF_UNLOAD:      return E1000_OnUnload();
        case DF_GETVERSION:  return E1000_OnGetVersion();
        case DF_GETCAPS:     return E1000_OnGetCaps();
        case DF_GETLASTFUNC: return E1000_OnGetLastFunc();

        // PCI binding
        case DF_PROBE:       return E1000_OnProbe((const PCI_INFO *)(LPVOID)Param);
        case DF_ATTACH:      return E1000_OnAttach((PCI_DEVICE *)(LPVOID)Param);
        case DF_DETACH:      return E1000_OnDetach((PCI_DEVICE *)(LPVOID)Param);

        // Network DF_* API
        case DF_NT_RESET:    return E1000_OnReset();
        case DF_NT_GETINFO:  return E1000_OnGetInfo((LPNETWORKINFO)(LPVOID)Param);
        case DF_NT_SETRXCB:  return E1000_OnSetRxCb((NT_RXCB)(LPVOID)Param);
        case DF_NT_SEND:     return E1000_OnSend((const NETWORKSEND *)(LPVOID)Param);
        case DF_NT_POLL:     return E1000_OnPoll();
    }

    return DF_ERROR_NOTIMPL;
}
