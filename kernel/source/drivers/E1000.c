
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


    E1000

\************************************************************************/

#include "drivers/E1000.h"

#include "Base.h"
#include "Driver.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Network.h"
#include "drivers/PCI.h"
#include "String.h"
#include "User.h"

/************************************************************************/

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

/************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

static U32 E1000Commands(U32 Function, U32 Param);

/************************************************************************/
// MMIO helpers

#define E1000_ReadReg32(Base, Off) (*(volatile U32 *)((U8 *)(Base) + (Off)))
#define E1000_WriteReg32(Base, Off, Val) (*(volatile U32 *)((U8 *)(Base) + (Off)) = (U32)(Val))

/************************************************************************/
// Small busy wait

static void E1000_Delay(U32 Iterations) {
    volatile U32 Index;
    for (Index = 0; Index < Iterations; Index++) {
        asm volatile("nop");
    }
}

/************************************************************************/
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

    // Pooled linear areas (one big allocation each)
    LINEAR RxPoolLinear;
    LINEAR TxPoolLinear;

    // RX callback (set via DF_NT_SETRXCB)
    NT_RXCB RxCallback;
    LPVOID RxUserData;
} E1000DEVICE, *LPE1000DEVICE;

/************************************************************************/
// Globals and PCI match table

static DRIVER_MATCH E1000_MatchTable[] = {E1000_MATCH_DEFAULT};

static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDev);

PCI_DRIVER E1000Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
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

/************************************************************************/
// EEPROM read and MAC

/**
 * @brief Read a 16-bit word from the device EEPROM.
 * @param Device Target E1000 device.
 * @param Address Word offset within the EEPROM.
 * @return Word value read from EEPROM.
 */
static U16 E1000_EepromReadWord(LPE1000DEVICE Device, U32 Address) {
    U32 Value = 0;
    U32 Count = 0;

    E1000_WriteReg32(Device->MmioBase, E1000_REG_EERD, ((Address & 0xFF) << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    while (Count < 100000) {
        Value = E1000_ReadReg32(Device->MmioBase, E1000_REG_EERD);
        if (Value & E1000_EERD_DONE) {
            // Successfully read, return the data
            return (U16)((Value >> E1000_EERD_DATA_SHIFT) & 0xFFFF);
        }
        Count++;
    }

    // EEPROM read failed/timed out - log error and return 0 as safe default
    ERROR(TEXT("[E1000_EepromReadWord] EEPROM read timeout at address %u after %u iterations"), Address, Count);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the MAC address from hardware or EEPROM.
 * @param Device Target E1000 device.
 */
static void E1000_ReadMac(LPE1000DEVICE Device) {
    U32 low = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAL0);
    U32 high = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAH0);

    DEBUG(TEXT("[E1000_ReadMac] Initial RAL0=%x RAH0=%x"), low, high);

    // Check if RAL/RAH contain a valid, non-zero MAC (AV bit set, not all zeros, not broadcast)
    if ((high & (1u << 31)) && (low != 0) &&
        !((low == 0xFFFFFFFF) && ((high & 0xFFFF) == 0xFFFF))) {
        // Additional check: ensure it's not a multicast address (first bit of first byte)
        U8 first_byte = (low >> 0) & 0xFF;
        if ((first_byte & 0x01) == 0) {
            // Valid unicast MAC address found in hardware registers
            Device->Mac[0] = (low >> 0) & 0xFF;
            Device->Mac[1] = (low >> 8) & 0xFF;
            Device->Mac[2] = (low >> 16) & 0xFF;
            Device->Mac[3] = (low >> 24) & 0xFF;
            Device->Mac[4] = (high >> 0) & 0xFF;
            Device->Mac[5] = (high >> 8) & 0xFF;
            DEBUG(TEXT("[E1000_ReadMac] Using valid RAL/RAH MAC: %X:%X:%X:%X:%X:%X"),
                  Device->Mac[0], Device->Mac[1], Device->Mac[2], Device->Mac[3], Device->Mac[4], Device->Mac[5]);
            return;
        }
    }

    // Fallback: read permanent MAC from EEPROM then program RAL/RAH
    DEBUG(TEXT("[E1000_ReadMac] Reading MAC from EEPROM"));
    U16 w0 = E1000_EepromReadWord(Device, 0);
    U16 w1 = E1000_EepromReadWord(Device, 1);
    U16 w2 = E1000_EepromReadWord(Device, 2);

    DEBUG(TEXT("[E1000_ReadMac] EEPROM words: w0=%x w1=%x w2=%x"), w0, w1, w2);

    if (w0 == 0 && w1 == 0 && w2 == 0) {
        // EEPROM is empty, use fallback MAC address
        DEBUG(TEXT("[E1000_ReadMac] EEPROM is empty, using fallback MAC"));
        Device->Mac[0] = 0x52;
        Device->Mac[1] = 0x54;
        Device->Mac[2] = 0x00;
        Device->Mac[3] = 0x12;
        Device->Mac[4] = 0x34;
        Device->Mac[5] = 0x56;
    } else {
        Device->Mac[0] = (U8)(w0 & 0xFF);
        Device->Mac[1] = (U8)(w0 >> 8);
        Device->Mac[2] = (U8)(w1 & 0xFF);
        Device->Mac[3] = (U8)(w1 >> 8);
        Device->Mac[4] = (U8)(w2 & 0xFF);
        Device->Mac[5] = (U8)(w2 >> 8);
    }

    low = (U32)Device->Mac[0] | ((U32)Device->Mac[1] << 8) | ((U32)Device->Mac[2] << 16) | ((U32)Device->Mac[3] << 24);
    high = (U32)Device->Mac[4] | ((U32)Device->Mac[5] << 8) | (1u << 31);  // Set AV (Address Valid) bit
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, low);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, high);

    DEBUG(TEXT("[E1000_ReadMac] Final MAC from EEPROM: %X:%X:%X:%X:%X:%X"),
          Device->Mac[0], Device->Mac[1], Device->Mac[2], Device->Mac[3], Device->Mac[4], Device->Mac[5]);
}

/************************************************************************/
// Core HW ops

/**
 * @brief Reset the network controller and configure basic settings.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_Reset(LPE1000DEVICE Device) {
    DEBUG(TEXT("[E1000_Reset] Begin"));
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

    DEBUG(TEXT("[E1000_Reset] Done"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Setup MAC address filters for packet reception.
 * @param Device Target E1000 device.
 */
static void E1000_SetupMacFilters(LPE1000DEVICE Device) {
    DEBUG(TEXT("[E1000_SetupMacFilters] Begin"));

    // Program our MAC address into Receive Address Register 0
    U32 RAL = (U32)Device->Mac[0] |
              ((U32)Device->Mac[1] << 8) |
              ((U32)Device->Mac[2] << 16) |
              ((U32)Device->Mac[3] << 24);

    U32 RAH = (U32)Device->Mac[4] |
              ((U32)Device->Mac[5] << 8) |
              (1U << 31);  // Address Valid bit

    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, RAL);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, RAH);

    DEBUG(TEXT("[E1000_SetupMacFilters] RAL0=%x RAH0=%x"), RAL, RAH);

    // Clear multicast table array (accept no multicast by default)
    for (U32 i = 0; i < 128; i++) {
        E1000_WriteReg32(Device->MmioBase, E1000_REG_MTA + (i * 4), 0);
    }

    DEBUG(TEXT("[E1000_SetupMacFilters] Done"));
}

/************************************************************************/
// RX/TX rings setup

/**
 * @brief Initialize the receive descriptor ring and buffers.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_SetupReceive(LPE1000DEVICE Device) {
    DEBUG(TEXT("[E1000_SetupReceive] Begin"));
    U32 Index;

    Device->RxRingCount = E1000_RX_DESC_COUNT;

    // Ring: one physical page, mapped once (unchanged semantics)
    Device->RxRingPhysical = AllocPhysicalPage();
    if (Device->RxRingPhysical == 0) {
        ERROR(TEXT("[E1000_SetupReceive] Rx ring phys alloc failed"));
        return FALSE;
    }
    Device->RxRingLinear =
        AllocKernelRegion(Device->RxRingPhysical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->RxRingLinear == 0) {
        ERROR(TEXT("[E1000_SetupReceive] Rx ring map failed"));
        return FALSE;
    }
    MemorySet((LPVOID)Device->RxRingLinear, 0, PAGE_SIZE);

    // RX buffer pool: allocate N pages in one shot (no target; VMM picks pages)
    Device->RxPoolLinear =
        AllocKernelRegion(0, E1000_RX_DESC_COUNT * PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->RxPoolLinear == 0) {
        ERROR(TEXT("[E1000_SetupReceive] Rx pool alloc failed"));
        return FALSE;
    }

    // Slice the pool per descriptor (1 page per buffer, as before)
    for (Index = 0; Index < Device->RxRingCount; Index++) {
        LINEAR la = Device->RxPoolLinear + (Index << PAGE_SIZE_MUL);
        PHYSICAL pa = MapLinearToPhysical(la);
        if (pa == 0) {
            ERROR(TEXT("[E1000_SetupReceive] Rx pool phys lookup failed at %u"), Index);
            return FALSE;
        }
        Device->RxBufLinear[Index] = la;
        Device->RxBufPhysical[Index] = pa;
    }

    // First, setup all descriptors before programming registers
    {
        LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingLinear;
        for (Index = 0; Index < Device->RxRingCount; Index++) {
            // Ensure physical addresses are properly aligned and valid
            PHYSICAL BufferPhys = Device->RxBufPhysical[Index];
            if (BufferPhys == 0 || (BufferPhys & 0xF) != 0) {
                ERROR(TEXT("[E1000_SetupReceive] Invalid/unaligned buffer physical address %x at index %u"),
                      (U32)BufferPhys, Index);
                return FALSE;
            }

            Ring[Index].BufferAddrLow = (U32)(BufferPhys & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Length = 0;
            Ring[Index].Checksum = 0;
            Ring[Index].Status = 0;
            Ring[Index].Errors = 0;
            Ring[Index].Special = 0;

            if (Index < 3) {
                DEBUG(TEXT("[E1000_SetupReceive] RX[%u]: PhysAddr=%x Linear=%x (aligned=%s)"),
                      Index, (U32)BufferPhys, (U32)Device->RxBufLinear[Index],
                      ((BufferPhys & 0xF) == 0) ? "YES" : "NO");
            }
        }

        // Additional verification: check descriptor ring alignment
        if ((Device->RxRingPhysical & 0xF) != 0) {
            ERROR(TEXT("[E1000_SetupReceive] Descriptor ring not 16-byte aligned: %x"),
                  (U32)Device->RxRingPhysical);
            return FALSE;
        }
        DEBUG(TEXT("[E1000_SetupReceive] Descriptor ring properly aligned at %x"),
              (U32)Device->RxRingPhysical);
    }

    // Then program NIC registers
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAL, (U32)(Device->RxRingPhysical & 0xFFFFFFFF));
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAH, 0);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDLEN, Device->RxRingCount * sizeof(E1000_RXDESC));

    // Initialize head and tail pointers
    // CRITICAL: RDT must point to the last available descriptor for HW to use
    Device->RxHead = 0;
    Device->RxTail = Device->RxRingCount - 1;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDH, 0);
    // Setting RDT to (count-1) tells HW all descriptors 0..count-1 are available
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, Device->RxTail);

    DEBUG(TEXT("[E1000_SetupReceive] Initial RDH=%u RDT=%u RingCount=%u"),
          Device->RxHead, Device->RxTail, Device->RxRingCount);

    // CRITICAL: Some QEMU versions require TCTL to be set before RCTL for proper link establishment
    // Set TCTL first with basic TX configuration
    U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
               (E1000_TCTL_CT_DEFAULT << E1000_TCTL_CT_SHIFT) |
               (E1000_TCTL_COLD_DEFAULT << E1000_TCTL_COLD_SHIFT);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TCTL, Tctl);
    DEBUG(TEXT("[E1000_SetupReceive] TCTL set to %x to establish link"), Tctl);

    {
        // Force promiscuous mode to capture all packets
        U32 Rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RCTL, Rctl);
        DEBUG(TEXT("[E1000_SetupReceive] RCTL set to %x (with promiscuous mode)"), Rctl);

        // Add small delay to let hardware stabilize
        E1000_Delay(100);

        // Verify register values
        U32 RctlRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RCTL);
        U32 RdhRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDH);
        U32 RdtRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDT);
        U32 RdlenRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDLEN);

        DEBUG(TEXT("[E1000_SetupReceive] RCTL=%x (expected=%x) RDH=%u RDT=%u RDLEN=%u"),
              RctlRead, Rctl, RdhRead, RdtRead, RdlenRead);

        // Verify ring base address registers
        U32 RdbalRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDBAL);
        U32 RdbahRead = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDBAH);
        DEBUG(TEXT("[E1000_SetupReceive] RDBAL=%x RDBAH=%x (expected phys=%x)"),
              RdbalRead, RdbahRead, (U32)Device->RxRingPhysical);

        // Debug first descriptor
        LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingLinear;
        DEBUG(TEXT("[E1000_SetupReceive] RX[0]: BufferAddr=%x:%x Status=%x"),
              Ring[0].BufferAddrHigh, Ring[0].BufferAddrLow, Ring[0].Status);

        // Check device status
        U32 StatusReg = E1000_ReadReg32(Device->MmioBase, E1000_REG_STATUS);
        DEBUG(TEXT("[E1000_SetupReceive] STATUS=%x LinkUp=%s FullDuplex=%s Speed=%s"),
              StatusReg,
              (StatusReg & E1000_STATUS_LU) ? "YES" : "NO",
              (StatusReg & E1000_STATUS_FD) ? "YES" : "NO",
              ((StatusReg >> 6) & 3) == 3 ? "1000" : ((StatusReg >> 6) & 3) == 2 ? "100" : "10");

        // QEMU E1000 compatibility: Force link up and configure TIPG
        DEBUG(TEXT("[E1000_SetupReceive] Applying QEMU E1000 compatibility fixes"));

        // Force link up without full device reset
        U32 Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
        Ctrl |= E1000_CTRL_SLU | E1000_CTRL_FD;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl);

        StatusReg = E1000_ReadReg32(Device->MmioBase, E1000_REG_STATUS);
        DEBUG(TEXT("[E1000_SetupReceive] After SLU: STATUS=%x LinkUp=%s"),
              StatusReg, (StatusReg & E1000_STATUS_LU) ? "YES" : "NO");

        // QEMU-specific TIPG configuration for proper packet timing
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TIPG, 0x00602008);
        DEBUG(TEXT("[E1000_SetupReceive] QEMU E1000 compatibility mode - ignoring link status"));
    }

    // Clear Multicast Table Array (MTA) - set all to 0 for unicast-only mode
    for (U32 i = 0; i < 128; i += 4) {
        E1000_WriteReg32(Device->MmioBase, E1000_REG_MTA + i, 0);
    }
    DEBUG(TEXT("[E1000_SetupReceive] Cleared Multicast Table Array"));

    // Set MAC address in Receive Address Register (RAL0/RAH0)
    U32 RalValue = (U32)Device->Mac[0] | ((U32)Device->Mac[1] << 8) | ((U32)Device->Mac[2] << 16) | ((U32)Device->Mac[3] << 24);
    U32 RahValue = (U32)Device->Mac[4] | ((U32)Device->Mac[5] << 8) | (1U << 31); // AV=1 (Address Valid)
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, RalValue);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, RahValue);
    DEBUG(TEXT("[E1000_SetupReceive] Set MAC address: RAL0=%x RAH0=%x"), RalValue, RahValue);

    // Enable RX interrupts to ensure proper packet reception
    // Clear any pending interrupts first
    E1000_ReadReg32(Device->MmioBase, E1000_REG_ICR);

    // Enable key interrupts: RXT (receive timeout), RXO (receive overrun), RXDMT (receive descriptor minimum threshold)
    U32 ImsValue = 0x80 | 0x40 | 0x10; // RXT0 | RXO | RXDMT0
    E1000_WriteReg32(Device->MmioBase, E1000_REG_IMS, ImsValue);
    DEBUG(TEXT("[E1000_SetupReceive] Enabled RX interrupts: IMS=%x"), ImsValue);

    DEBUG(TEXT("[E1000_SetupReceive] Done"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the transmit descriptor ring and buffers.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_SetupTransmit(LPE1000DEVICE Device) {
    DEBUG(TEXT("[E1000_SetupTransmit] Begin"));
    U32 Index;

    Device->TxRingCount = E1000_TX_DESC_COUNT;

    // Ring: one physical page, mapped once (unchanged semantics)
    Device->TxRingPhysical = AllocPhysicalPage();
    if (Device->TxRingPhysical == 0) {
        ERROR(TEXT("[E1000_SetupTransmit] Tx ring phys alloc failed"));
        return FALSE;
    }
    Device->TxRingLinear =
        AllocKernelRegion(Device->TxRingPhysical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->TxRingLinear == 0) {
        ERROR(TEXT("[E1000_SetupTransmit] Tx ring map failed"));
        return FALSE;
    }
    MemorySet((LPVOID)Device->TxRingLinear, 0, PAGE_SIZE);

    // TX buffer pool: allocate N pages in one shot
    Device->TxPoolLinear =
        AllocKernelRegion(0, E1000_TX_DESC_COUNT * PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    if (Device->TxPoolLinear == 0) {
        ERROR(TEXT("[E1000_SetupTransmit] Tx pool alloc failed"));
        return FALSE;
    }

    for (Index = 0; Index < Device->TxRingCount; Index++) {
        LINEAR la = Device->TxPoolLinear + (Index << PAGE_SIZE_MUL);
        PHYSICAL pa = MapLinearToPhysical(la);
        if (pa == 0) {
            ERROR(TEXT("[E1000_SetupTransmit] Tx pool phys lookup failed at %u"), Index);
            return FALSE;
        }
        Device->TxBufLinear[Index] = la;
        Device->TxBufPhysical[Index] = pa;
    }

    // Setup descriptors and verify TX buffer alignment
    {
        LPE1000_TXDESC Ring = (LPE1000_TXDESC)Device->TxRingLinear;
        for (Index = 0; Index < Device->TxRingCount; Index++) {
            // Ensure TX physical addresses are properly aligned and valid
            PHYSICAL BufferPhys = Device->TxBufPhysical[Index];
            if (BufferPhys == 0 || (BufferPhys & 0xF) != 0) {
                ERROR(TEXT("[E1000_SetupTransmit] Invalid/unaligned TX buffer physical address %x at index %u"),
                      (U32)BufferPhys, Index);
                return FALSE;
            }

            Ring[Index].BufferAddrLow = (U32)(BufferPhys & 0xFFFFFFFF);
            Ring[Index].BufferAddrHigh = 0;
            Ring[Index].Length = 0;
            Ring[Index].CSO = 0;
            Ring[Index].CMD = 0;
            Ring[Index].STA = E1000_TX_STA_DD; // Mark descriptor as done/available
            Ring[Index].CSS = 0;
            Ring[Index].Special = 0;

            if (Index < 3) {
                DEBUG(TEXT("[E1000_SetupTransmit] TX[%u]: PhysAddr=%x Linear=%x (aligned=%s)"),
                      Index, (U32)BufferPhys, (U32)Device->TxBufLinear[Index],
                      ((BufferPhys & 0xF) == 0) ? "YES" : "NO");
            }
        }

        // Additional verification: check TX descriptor ring alignment
        if ((Device->TxRingPhysical & 0xF) != 0) {
            ERROR(TEXT("[E1000_SetupTransmit] TX descriptor ring not 16-byte aligned: %x"),
                  (U32)Device->TxRingPhysical);
            return FALSE;
        }
        DEBUG(TEXT("[E1000_SetupTransmit] TX descriptor ring properly aligned at %x"),
              (U32)Device->TxRingPhysical);
    }

    // Program NIC registers
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAL, (U32)(Device->TxRingPhysical & 0xFFFFFFFF));
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAH, 0);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDLEN, Device->TxRingCount * sizeof(E1000_TXDESC));

    // Initialize head and tail pointers
    Device->TxHead = 0;
    Device->TxTail = 0;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDH, Device->TxHead);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, Device->TxTail);

    // Enable TX
    {
        U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (E1000_TCTL_CT_DEFAULT << E1000_TCTL_CT_SHIFT) |
                   (E1000_TCTL_COLD_DEFAULT << E1000_TCTL_COLD_SHIFT);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TCTL, Tctl);
    }

    DEBUG(TEXT("[E1000_SetupTransmit] Done"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDev PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDevice) {
    DEBUG(TEXT("[E1000_Attach] New device %x:%x.%u"), (U32)PciDevice->Info.Bus, (U32)PciDevice->Info.Dev,
        (U32)PciDevice->Info.Func);

    LPE1000DEVICE Device = (LPE1000DEVICE)KernelHeapAlloc(sizeof(E1000DEVICE));
    if (Device == NULL) return NULL;

    MemorySet(Device, 0, sizeof(E1000DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));

    DEBUG(TEXT("[E1000_Attach] Device=%x, ID=%x, PciDevice->TypeID=%x"), Device, Device->TypeID, PciDevice->TypeID);

    U32 Bar0Phys = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);

    DEBUG(TEXT("[E1000_Attach] BAR0: Phys=%x Size=%x"), Bar0Phys, Bar0Size);

    if (Bar0Phys == NULL || Bar0Size == 0) {
        ERROR(TEXT("[E1000_Attach] Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[E1000_Attach] Calling MapIOMemory(Phys=%X, Size=%X)"), Bar0Phys, Bar0Size);

    Device->MmioBase = MapIOMemory(Bar0Phys, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == NULL) {
        ERROR(TEXT("[E1000_Attach] MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[E1000_Attach] MMIO mapped at %X size %X"), Device->MmioBase, Device->MmioSize);

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 1);

    if (!E1000_Reset(Device)) {
        ERROR(TEXT("[E1000_Attach] Reset failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[E1000_Attach] Reset complete"));

    E1000_ReadMac(Device);

    // Setup MAC address filters
    E1000_SetupMacFilters(Device);

    if (!E1000_SetupReceive(Device)) {
        ERROR(TEXT("[E1000_Attach] RX setup failed"));
        if (Device->MmioBase) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        }
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[E1000_Attach] RX setup complete"));

    if (!E1000_SetupTransmit(Device)) {
        ERROR(TEXT("[E1000_Attach] TX setup failed"));
        // Cleanup RX resources
        if (Device->RxRingLinear) {
            FreeRegion(Device->RxRingLinear, PAGE_SIZE);
        }
        if (Device->RxRingPhysical) {
            FreePhysicalPage(Device->RxRingPhysical);
        }
        if (Device->RxPoolLinear) {
            FreeRegion(Device->RxPoolLinear, E1000_RX_DESC_COUNT * PAGE_SIZE);
        }
        if (Device->MmioBase) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        }
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[E1000_Attach] TX setup complete"));
    DEBUG(TEXT("[E1000_Attach] Attached %X:%X.%u MMIO=%X size=%X MAC=%X:%X:%X:%X:%X:%X"), (U32)Device->Info.Bus,
        (U32)Device->Info.Dev, (U32)Device->Info.Func, (U32)Device->MmioBase, (U32)Device->MmioSize,
        (U32)Device->Mac[0], (U32)Device->Mac[1], (U32)Device->Mac[2], (U32)Device->Mac[3], (U32)Device->Mac[4],
        (U32)Device->Mac[5]);

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/
// Receive/Transmit operations

/**
 * @brief Send a frame using the transmit ring.
 * @param Device Target E1000 device.
 * @param Data Pointer to frame data.
 * @param Length Length of frame in bytes.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_TransmitSend(LPE1000DEVICE Device, const U8 *Data, U32 Length) {
    if (Length == 0 || Length > E1000_TX_BUF_SIZE) return DF_ERROR_BADPARAM;

    DEBUG(TEXT("[E1000_TransmitSend] ENTRY len=%u TxTail=%u"), (U32)Length, Device->TxTail);

    U32 Index = Device->TxTail;
    LPE1000_TXDESC Ring = (LPE1000_TXDESC)Device->TxRingLinear;

    // Log buffer addresses and TX ring state before
    DEBUG(TEXT("[E1000_TransmitSend] Index=%u TxBufPhys=%x TxBufLinear=%x"), Index, Device->TxBufPhysical[Index], Device->TxBufLinear[Index]);
    DEBUG(TEXT("[E1000_TransmitSend] BEFORE: Ring[%u].Addr=%x:%x Length=%u CMD=%x STA=%x"),
          Index, Ring[Index].BufferAddrHigh, Ring[Index].BufferAddrLow, Ring[Index].Length, Ring[Index].CMD, Ring[Index].STA);

    // Copy into pre-allocated TX buffer
    MemoryCopy((LPVOID)Device->TxBufLinear[Index], (LPVOID)Data, Length);
    DEBUG(TEXT("[E1000_TransmitSend] Data copied to buffer, first 4 bytes: %02x%02x%02x%02x"),
          Data[0], Data[1], Data[2], Data[3]);

    Ring[Index].Length = (U16)Length;
    Ring[Index].CMD = (E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    Ring[Index].STA = 0;

    DEBUG(TEXT("[E1000_TransmitSend] AFTER setup: Ring[%u].Length=%u CMD=%x STA=%x"),
          Index, Ring[Index].Length, Ring[Index].CMD, Ring[Index].STA);

    // Read current TDT before write
    U32 CurrentTDT = E1000_ReadReg32(Device->MmioBase, E1000_REG_TDT);
    DEBUG(TEXT("[E1000_TransmitSend] Current TDT=%u"), CurrentTDT);

    // Advance tail
    U32 NewTail = (Index + 1) % Device->TxRingCount;
    Device->TxTail = NewTail;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, NewTail);

    // Verify TDT write
    U32 VerifyTDT = E1000_ReadReg32(Device->MmioBase, E1000_REG_TDT);
    DEBUG(TEXT("[E1000_TransmitSend] TDT written=%u verified=%u"), NewTail, VerifyTDT);

    // Check TX registers
    U32 TDH = E1000_ReadReg32(Device->MmioBase, E1000_REG_TDH);
    U32 TCTL = E1000_ReadReg32(Device->MmioBase, E1000_REG_TCTL);
    DEBUG(TEXT("[E1000_TransmitSend] TDH=%u TDT=%u TCTL=%x"), TDH, VerifyTDT, TCTL);

    // Simple spin for DD
    U32 Wait = 0;
    while (((Ring[Index].STA & E1000_TX_STA_DD) == 0) && (Wait++ < 100000)) {
    }

    DEBUG(TEXT("[E1000_TransmitSend] FINAL: Wait=%u Ring[%u].STA=%x DD=%s"),
          Wait, Index, Ring[Index].STA, (Ring[Index].STA & E1000_TX_STA_DD) ? "YES" : "NO");

    if (Wait >= 100000) {
        ERROR(TEXT("[E1000_TransmitSend] TX timeout - packet transmission failed"));
        return DF_ERROR_NT_TX_FAIL;
    }

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Poll the receive ring for incoming frames.
 * @param Device Target E1000 device.
 * @return DF_ERROR_SUCCESS after processing frames.
 */
static U32 E1000_ReceivePoll(LPE1000DEVICE Device) {
    LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingLinear;
    U32 Count = 0;
    U32 MaxIterations = Device->RxRingCount * 2; // Safety limit: twice the ring size
    U32 ConsecutiveEmptyChecks = 0;

    // DEBUG(TEXT("[E1000_ReceivePoll] Enter - Device=%x RxCallback=%x RxHead=%u"), (U32)Device, (U32)Device->RxCallback, Device->RxHead);

    while (Count < MaxIterations) {
        U32 NextIndex = (Device->RxHead) % Device->RxRingCount;
        U8 Status = Ring[NextIndex].Status;

        // DEBUG(TEXT("[E1000_ReceivePoll] Index=%u, Status=%x, DD=%u, EOP=%u"), NextIndex, Status, (Status & E1000_RX_STA_DD) ? 1 : 0, (Status & E1000_RX_STA_EOP) ? 1 : 0);

        if ((Status & E1000_RX_STA_DD) == 0) {
            ConsecutiveEmptyChecks++;
            // If we've checked multiple times with no new packets, break to prevent spinning
            if (ConsecutiveEmptyChecks >= 3) {
                // No data available - show RX register state every 100 polls
                static U32 PollCount = 0;
                if ((PollCount++ % 100) == 0) {
                    U32 RDH = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDH);
                    U32 RDT = E1000_ReadReg32(Device->MmioBase, E1000_REG_RDT);
                    U32 RCTL = E1000_ReadReg32(Device->MmioBase, E1000_REG_RCTL);
                    DEBUG(TEXT("[E1000_ReceivePoll] No packets: RxHead=%u NextIndex=%u Status=%x RDH=%u RDT=%u RCTL=%x"),
                          Device->RxHead, NextIndex, Status, RDH, RDT, RCTL);
                }
                break;
            }
            // Small delay to let hardware potentially update descriptor
            volatile U32 delay;
            for (delay = 0; delay < 10; delay++) {
                asm volatile("nop");
            }
            continue;
        }

        // Reset consecutive empty checks since we found a packet
        ConsecutiveEmptyChecks = 0;

        DEBUG(TEXT("[E1000_ReceivePoll] Packet received at index %u, status=%X"), NextIndex, Status);

        if ((Status & E1000_RX_STA_EOP) != 0) {
            U16 Length = Ring[NextIndex].Length;
            const U8 *Frame = (const U8 *)Device->RxBufLinear[NextIndex];
            DEBUG(TEXT("[E1000_ReceivePoll] Frame length=%u, EthType=%x%x, RxCallback=%x"),
                  Length, Frame[12], Frame[13], (U32)Device->RxCallback);
            if (Device->RxCallback) {
                DEBUG(TEXT("[E1000_ReceivePoll] Calling RxCallback at %x"), (U32)Device->RxCallback);
                Device->RxCallback(Frame, (U32)Length, Device->RxUserData);
                DEBUG(TEXT("[E1000_ReceivePoll] RxCallback returned"));
            } else {
                DEBUG(TEXT("[E1000_ReceivePoll] No RX callback registered!"));
            }
        }

        // Advance head
        Device->RxHead = (NextIndex + 1) % Device->RxRingCount;

        // RDT must point to the last descriptor that the hardware can use
        // Make the processed descriptor available again by updating RDT to point to it
        Device->RxTail = NextIndex;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, NextIndex);

        DEBUG(TEXT("[E1000_ReceivePoll] Updated RDT to %u (processed descriptor available for reuse)"), NextIndex);

        // Clear descriptor status AFTER updating RDT to avoid race condition
        Ring[NextIndex].Status = 0;

        Count++;
    }

    if (Count >= MaxIterations) {
        WARNING(TEXT("[E1000_ReceivePoll] Hit maximum iteration limit (%u), potential infinite loop prevented"), MaxIterations);
    }

    // DEBUG(TEXT("[E1000_ReceivePoll] Exit - processed %u packets"), Count);
    return DF_ERROR_SUCCESS;
}

/************************************************************************/
// PCI-level helpers (per-function)

/**
 * @brief Verify PCI information matches supported hardware.
 * @param PciInfo PCI configuration to probe.
 * @return DF_ERROR_SUCCESS if supported, otherwise DF_ERROR_NOTIMPL.
 */
static U32 E1000_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo->VendorID != E1000_VENDOR_INTEL) return DF_ERROR_NOTIMPL;
    if (PciInfo->DeviceID != E1000_DEVICE_82540EM) return DF_ERROR_NOTIMPL;
    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) return DF_ERROR_NOTIMPL;
    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) return DF_ERROR_NOTIMPL;
    return DF_ERROR_SUCCESS;
}

/************************************************************************/
// Network DF_* helpers (per-function)

/**
 * @brief Reset callback for network stack.
 * @param Reset Reset parameters.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_OnReset(const NETWORKRESET *Reset) {
    if (Reset == NULL || Reset->Device == NULL) return DF_ERROR_BADPARAM;
    return E1000_Reset((LPE1000DEVICE)Reset->Device) ? DF_ERROR_SUCCESS : DF_ERROR_UNEXPECT;
}

/************************************************************************/

/**
 * @brief Fill NETWORKINFO structure with device state.
 * @param Get Query parameters and output buffer.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_OnGetInfo(const NETWORKGETINFO *Get) {
    DEBUG(TEXT("[E1000_OnGetInfo] Enter"));
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

    DEBUG(TEXT("[E1000_OnGetInfo] MAC copied: %x:%x:%x:%x:%x:%x"),
          Get->Info->MAC[0], Get->Info->MAC[1], Get->Info->MAC[2],
          Get->Info->MAC[3], Get->Info->MAC[4], Get->Info->MAC[5]);
    DEBUG(TEXT("[E1000_OnGetInfo] Link=%s Speed=%u Duplex=%s MTU=%u"),
          Get->Info->LinkUp ? "UP" : "DOWN", Get->Info->SpeedMbps,
          Get->Info->DuplexFull ? "FULL" : "HALF", Get->Info->MTU);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Register a callback for received frames.
 * @param Set Parameters including callback pointer.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_OnSetReceiveCallback(const NETWORKSETRXCB *Set) {
    DEBUG(TEXT("[E1000_OnSetReceiveCallback] Entry Set=%X"), (U32)Set);
    if (Set == NULL || Set->Device == NULL) {
        DEBUG(TEXT("[E1000_OnSetReceiveCallback] Bad parameters: Set=%X Device=%X"), (U32)Set, Set ? (U32)Set->Device : 0);
        return DF_ERROR_BADPARAM;
    }
    LPE1000DEVICE Device = (LPE1000DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    Device->RxUserData = Set->UserData;
    DEBUG(TEXT("[E1000_OnSetReceiveCallback] Callback set to %X with UserData %X for device %X"), (U32)Set->Callback, (U32)Set->UserData, (U32)Device);
    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Send frame through network stack interface.
 * @param Send Parameters describing frame to send.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_OnSend(const NETWORKSEND *Send) {
    DEBUG(TEXT("[E1000_OnSend] Entry: Send=%x"), Send);
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        DEBUG(TEXT("[E1000_OnSend] ERROR: Bad parameters"));
        return DF_ERROR_BADPARAM;
    }
    DEBUG(TEXT("[E1000_OnSend] Calling TxSend: Device=%x, Length=%u"), Send->Device, Send->Length);
    U32 result = E1000_TransmitSend((LPE1000DEVICE)Send->Device, Send->Data, Send->Length);
    DEBUG(TEXT("[E1000_OnSend] TxSend result: %u"), result);
    return result;
}

/************************************************************************/

/**
 * @brief Poll device for received frames through network stack interface.
 * @param Poll Poll parameters.
 * @return DF_ERROR_SUCCESS on success or error code.
 */
static U32 E1000_OnPoll(const NETWORKPOLL *Poll) {
    if (Poll == NULL || Poll->Device == NULL) return DF_ERROR_BADPARAM;
    return E1000_ReceivePoll((LPE1000DEVICE)Poll->Device);
}

/************************************************************************/
// Driver meta helpers

/**
 * @brief Driver load callback.
 * @return DF_ERROR_SUCCESS.
 */
static U32 E1000_OnLoad(void) { return DF_ERROR_SUCCESS; }

/************************************************************************/

/**
 * @brief Driver unload callback.
 * @return DF_ERROR_SUCCESS.
 */
static U32 E1000_OnUnload(void) { return DF_ERROR_SUCCESS; }

/************************************************************************/

/**
 * @brief Retrieve driver version encoded with MAKE_VERSION.
 * @return Encoded version number.
 */
static U32 E1000_OnGetVersion(void) { return MAKE_VERSION(VER_MAJOR, VER_MINOR); }

/************************************************************************/

/**
 * @brief Report driver capabilities bitmask.
 * @return Capability flags, zero if none.
 */
static U32 E1000_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Return last implemented DF_* function.
 * @return Function identifier used for iteration.
 */
static U32 E1000_OnGetLastFunc(void) { return DF_NT_POLL; }

/************************************************************************/
// Driver entry

/**
 * @brief Central dispatch for all driver functions.
 * @param Function Identifier of requested driver operation.
 * @param Param Optional pointer to parameters.
 * @return DF_ERROR_* code depending on operation.
 */
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
            return E1000_OnSetReceiveCallback((const NETWORKSETRXCB *)(LPVOID)Param);
        case DF_NT_SEND:
            return E1000_OnSend((const NETWORKSEND *)(LPVOID)Param);
        case DF_NT_POLL:
            return E1000_OnPoll((const NETWORKPOLL *)(LPVOID)Param);
    }

    return DF_ERROR_NOTIMPL;
}
