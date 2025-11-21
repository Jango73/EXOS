
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


    SATA - AHCI Implementation

\************************************************************************/

#include "drivers/SATA.h"

#include "Clock.h"
#include "DeviceInterrupt.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/PCI.h"
#include "User.h"
#include "utils/Cache.h"

/***************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/
// Additional error codes not in User.h

#define DF_ERROR_HARDWARE 0x00001001
#define DF_ERROR_TIMEOUT 0x00001002
#define DF_ERROR_BUSY 0x00001003
#define DF_ERROR_NODEVICE 0x00001004

/***************************************************************************/

UINT SATADiskCommands(UINT, UINT);

DRIVER DATA_SECTION SATADiskDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_HARDDISK,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "AHCI Controllers",
    .Product = "AHCI SATA Controller",
    .Flags = 0,
    .Command = SATADiskCommands};

/***************************************************************************/

#define AHCI_MAX_PORTS 32
#define AHCI_CMD_LIST_SIZE 1024  // 32 command headers * 32 bytes each
#define AHCI_FIS_SIZE 256        // FIS receive area size
#define AHCI_CMD_TBL_SIZE 256    // Command table size (including PRDT)
#define AHCI_MAX_PRDT 8          // Max PRDT entries per command

/***************************************************************************/
// AHCI Port Structure

typedef struct tag_AHCI_PORT {
    PHYSICALDISK Header;
    DISKGEOMETRY Geometry;
    U32 Access;  // Access parameters
    U32 PortNumber;
    LPAHCI_HBA_PORT HBAPort; // Pointer to HBA port registers
    LPAHCI_HBA_MEM HBAMem;   // Pointer to HBA memory

    // Command structures (must be aligned)
    LPAHCI_CMD_HEADER CommandList;  // Command list (1KB aligned)
    LPAHCI_FIS FISBase;             // FIS receive area (256-byte aligned)
    LPAHCI_CMD_TBL CommandTable;    // Command table

    // Buffer management
    CACHE SectorCache;

    volatile U32 PendingInterrupts;
} AHCI_PORT, *LPAHCI_PORT;

/***************************************************************************/
// Global AHCI state

typedef struct tag_AHCI_STATE {
    LPAHCI_HBA_MEM Base;
    U32 PortsImplemented;
    LPPCI_DEVICE Device;
    LPAHCI_PORT Ports[AHCI_MAX_PORTS];
    volatile U32 PendingPortsMask;
    U8 InterruptSlot;
    BOOL InterruptRegistered;
    BOOL InterruptEnabled;
} AHCI_STATE, *LPAHCI_STATE;

static AHCI_STATE AHCIState = {
    .Base = NULL,
    .PortsImplemented = 0,
    .Device = NULL,
    .Ports = {0},
    .PendingPortsMask = 0,
    .InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT,
    .InterruptRegistered = FALSE,
    .InterruptEnabled = FALSE};

/***************************************************************************/
// AHCI PCI Driver

static U32 AHCIProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE AHCIAttach(LPPCI_DEVICE PciDevice);
static U32 InitializeAHCIController(void);
static BOOL AHCIRegisterInterrupts(void);
static BOOL AHCIInterruptTopHalf(LPDEVICE Device, LPVOID Context);
static void AHCIInterruptBottomHalf(LPDEVICE Device, LPVOID Context);
static void AHCIInterruptPoll(LPDEVICE Device, LPVOID Context);

static const DRIVER_MATCH AHCIMatches[] = {
    // Match any AHCI controller (Class 01h, Subclass 06h, Programming Interface 01h)
    {PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE, 0x06, 0x01}
};

PCI_DRIVER AHCIPCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_HARDDISK,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "AHCI Controllers",
    .Product = "AHCI SATA Controller",
    .Command = AHCIProbe,
    .Matches = AHCIMatches,
    .MatchCount = 1,
    .Attach = AHCIAttach
};

/***************************************************************************/

typedef struct tag_SATA_CACHE_CONTEXT {
    U32 SectorLow;
    U32 SectorHigh;
} SATA_CACHE_CONTEXT, *LPSATA_CACHE_CONTEXT;

/***************************************************************************/

/**
 * @brief Matcher callback for SATA sector cache entries.
 *
 * @param Data Cache entry (LPSECTORBUFFER).
 * @param Context Matching context with sector range.
 * @return TRUE if entry matches requested sector range.
 */
static BOOL SATACacheMatcher(LPVOID Data, LPVOID Context) {
    LPSECTORBUFFER Buffer = (LPSECTORBUFFER)Data;
    LPSATA_CACHE_CONTEXT Match = (LPSATA_CACHE_CONTEXT)Context;

    if (Buffer == NULL || Match == NULL) {
        return FALSE;
    }

    return Buffer->SectorLow == Match->SectorLow && Buffer->SectorHigh == Match->SectorHigh;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize an AHCI port structure.
 *
 * @return Pointer to new port or NULL on failure.
 */
static LPAHCI_PORT NewAHCIPort(void) {
    LPAHCI_PORT This;

    This = (LPAHCI_PORT)KernelHeapAlloc(sizeof(AHCI_PORT));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(AHCI_PORT));

    This->Header.TypeID = KOID_DISK;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &SATADiskDriver;
    This->Access = 0;
    This->PendingInterrupts = 0;

    return This;
}

/***************************************************************************/

/**
 * @brief Stop an AHCI port (disable command/FIS engines).
 *
 * Clears ST and FRE then waits for hardware to acknowledge.
 *
 * @param Port Target HBA port.
 */
static void StopPort(LPAHCI_HBA_PORT Port) {
    // Clear ST (Start) bit
    Port->cmd &= ~AHCI_PORT_CMD_ST;

    // Wait until CR (Command List Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_CR) {
        // Wait
    }

    // Clear FRE (FIS Receive Enable)
    Port->cmd &= ~AHCI_PORT_CMD_FRE;

    // Wait until FR (FIS Receive Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_FR) {
        // Wait
    }
}

/***************************************************************************/

/**
 * @brief Start an AHCI port (enable command/FIS engines).
 *
 * @param Port Target HBA port.
 */
static void StartPort(LPAHCI_HBA_PORT Port) {
    // Wait until CR (Command List Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_CR) {
        // Wait
    }

    // Set FRE (FIS Receive Enable)
    Port->cmd |= AHCI_PORT_CMD_FRE;

    // Set ST (Start) bit
    Port->cmd |= AHCI_PORT_CMD_ST;
}

/***************************************************************************/

/*
static U32 FindFreeCommandSlot(LPAHCI_HBA_PORT Port) {
    // Check which slots are free
    U32 slots = (Port->sact | Port->ci);
    U32 cmdslots = (AHCIState.Base->cap & AHCI_CAP_NCS_MASK) >> 8;

    for (U32 i = 0; i < cmdslots; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }

    return MAX_U32; // No free slot found
}
*/

/***************************************************************************/

/**
 * @brief Reset an AHCI port and ensure device presence.
 *
 * @param Port Target HBA port.
 * @return TRUE if reset sequence succeeds and device present.
 */
static BOOL AHCIPortReset(LPAHCI_HBA_PORT Port) {
    U32 timeout = 1000000; // 1 second timeout

    // Check if device is present
    if ((Port->ssts & AHCI_PORT_SSTS_DET_MASK) != AHCI_PORT_SSTS_DET_ESTABLISHED) {
        return FALSE;
    }

    // Perform COMRESET
    Port->sctl = (Port->sctl & ~0xF) | 0x1; // Set DET to 1

    // Wait 1ms
    for (volatile U32 i = 0; i < 10000; i++) {
        // Busy wait
    }

    Port->sctl &= ~0xF; // Clear DET

    // Wait for device to be ready
    while (timeout--) {
        if ((Port->ssts & AHCI_PORT_SSTS_DET_MASK) == AHCI_PORT_SSTS_DET_ESTABLISHED) {
            break;
        }
    }

    if (timeout == 0) {
        return FALSE;
    }

    // Clear error register
    Port->serr = 0xFFFFFFFF;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize an AHCI port and allocate per-port structures.
 *
 * Verifies presence, stops the port, allocates command list/FIS/command table,
 * and performs device identification when possible.
 *
 * @param AHCIPort Port wrapper structure.
 * @param PortNum Port index.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL InitializeAHCIPort(LPAHCI_PORT AHCIPort, U32 PortNum) {
    LPAHCI_HBA_PORT Port = &AHCIState.Base->ports[PortNum];

    DEBUG(TEXT("[InitializeAHCIPort] Initializing port %u"), PortNum);
    AHCIState.Ports[PortNum] = NULL;

    // Check if port is implemented
    if ((AHCIState.PortsImplemented & (1 << PortNum)) == 0) {
        return FALSE;
    }

    // Check if device is present
    U32 ssts = Port->ssts;
    U32 det = ssts & AHCI_PORT_SSTS_DET_MASK;
    DEBUG(TEXT("[InitializeAHCIPort] Port %u SSTS: %x, DET: %x"), PortNum, ssts, det);
    DEBUG(TEXT("[InitializeAHCIPort] Expected DET_ESTABLISHED: %x"), AHCI_PORT_SSTS_DET_ESTABLISHED);

    if (det == AHCI_PORT_SSTS_DET_NONE) {
        DEBUG(TEXT("[InitializeAHCIPort] No device on port %u (DET=%x)"), PortNum, det);
        return FALSE;
    }

    if (det == AHCI_PORT_SSTS_DET_PRESENT) {
        DEBUG(TEXT("[InitializeAHCIPort] Device present on port %u but communication not established, continuing..."), PortNum);
    } else if (det == AHCI_PORT_SSTS_DET_ESTABLISHED) {
        DEBUG(TEXT("[InitializeAHCIPort] Device communication established on port %u"), PortNum);
    } else {
        DEBUG(TEXT("[InitializeAHCIPort] Unknown DET state %x on port %u"), det, PortNum);
    }

    // Stop port
    StopPort(Port);

    // Allocate command list (use regular allocation for now)
    AHCIPort->CommandList = (LPAHCI_CMD_HEADER)KernelHeapAlloc(AHCI_CMD_LIST_SIZE);
    if (AHCIPort->CommandList == NULL) {
        DEBUG(TEXT("[InitializeAHCIPort] Failed to allocate command list"));
        return FALSE;
    }
    MemorySet(AHCIPort->CommandList, 0, AHCI_CMD_LIST_SIZE);

    // Allocate FIS receive area (use regular allocation for now)
    AHCIPort->FISBase = (LPAHCI_FIS)KernelHeapAlloc(AHCI_FIS_SIZE);
    if (AHCIPort->FISBase == NULL) {
        DEBUG(TEXT("[InitializeAHCIPort] Failed to allocate FIS area"));
        return FALSE;
    }
    MemorySet(AHCIPort->FISBase, 0, AHCI_FIS_SIZE);

    // Allocate command table
    AHCIPort->CommandTable = (LPAHCI_CMD_TBL)KernelHeapAlloc(AHCI_CMD_TBL_SIZE);
    if (AHCIPort->CommandTable == NULL) {
        DEBUG(TEXT("[InitializeAHCIPort] Failed to allocate command table"));
        return FALSE;
    }
    MemorySet(AHCIPort->CommandTable, 0, AHCI_CMD_TBL_SIZE);

    CacheInit(&AHCIPort->SectorCache, NUM_BUFFERS);
    if (AHCIPort->SectorCache.Entries == NULL) {
        DEBUG(TEXT("[InitializeAHCIPort] Failed to initialize cache"));
        return FALSE;
    }

    // Set up port registers with physical addresses for DMA
    PHYSICAL CommandListPhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandList);
    PHYSICAL FISBasePhys = MapLinearToPhysical((LINEAR)AHCIPort->FISBase);
    PHYSICAL CommandTablePhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandTable);

    Port->clb = CommandListPhys;
    Port->clbu = 0; // Assume 32-bit system
    Port->fb = FISBasePhys;
    Port->fbu = 0; // Assume 32-bit system

    DEBUG(TEXT("[InitializeAHCIPort] CommandList: virt=%x phys=%x"),
          (U32)AHCIPort->CommandList, (LINEAR)CommandListPhys);
    DEBUG(TEXT("[InitializeAHCIPort] FISBase: virt=%x phys=%x"),
          (U32)AHCIPort->FISBase, (LINEAR)FISBasePhys);

    // Set up command header for slot 0
    AHCIPort->CommandList[0].cfl = sizeof(FIS_REG_H2D) / 4; // FIS length
    AHCIPort->CommandList[0].prdtl = 1; // One PRDT entry
    AHCIPort->CommandList[0].ctba = CommandTablePhys;
    AHCIPort->CommandList[0].ctbau = 0;

    // Store references
    AHCIPort->PortNumber = PortNum;
    AHCIPort->HBAPort = Port;
    AHCIPort->HBAMem = AHCIState.Base;
    AHCIPort->PendingInterrupts = 0;
    AHCIState.Ports[PortNum] = AHCIPort;

    // Clear any pending interrupt sources and keep the port masked. AHCI
    // commands are handled synchronously so INTx lines must stay quiet when
    // other devices (E1000) reuse the same legacy IRQ.
    Port->is = 0xFFFFFFFF;
    Port->ie = 0x0;

    // Reset port
    if (!AHCIPortReset(Port)) {
        DEBUG(TEXT("[InitializeAHCIPort] Port reset failed"));
        return FALSE;
    }

    // Start port
    StartPort(Port);

    // Try to identify the device
    // For now, assume it's a standard SATA disk
    AHCIPort->Geometry.Cylinders = 1024;
    AHCIPort->Geometry.Heads = 16;
    AHCIPort->Geometry.SectorsPerTrack = 63;
    AHCIPort->Geometry.BytesPerSector = SECTOR_SIZE;

    DEBUG(TEXT("[InitializeAHCIPort] Port %u initialized successfully"), PortNum);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief PCI probe entry for the AHCI driver.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return DF_ERROR_SUCCESS on handled, DF_ERROR_NOTIMPL otherwise.
 */
static U32 AHCIProbe(UINT Function, UINT Parameter) {
    LPPCI_INFO PciInfo = (LPPCI_INFO)Parameter;

    if (Function != DF_PROBE) {
        return DF_ERROR_NOTIMPL;
    }

    if (PciInfo == NULL) {
        return DF_ERROR_BADPARAM;
    }

    DEBUG(TEXT("[AHCIProbe] Found AHCI controller %x:%x"),
          (INT)PciInfo->VendorID, (INT)PciInfo->DeviceID);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Attach detected AHCI controller and initialize state.
 *
 * Allocates PCI device copy, maps ABAR, enumerates ports, and registers interrupts.
 *
 * @param PciDevice Detected PCI device descriptor.
 * @return Heap-allocated PCI device pointer or NULL on failure.
 */
static LPPCI_DEVICE AHCIAttach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    // Allocate a new device structure on kernel heap as required by PCI driver specification
    LPPCI_DEVICE Device = (LPPCI_DEVICE)KernelHeapAlloc(sizeof(PCI_DEVICE));
    if (Device == NULL) {
        DEBUG(TEXT("[AHCIAttach] Failed to allocate device structure"));
        return NULL;
    }

    // Copy PCI device information to the heap-allocated structure
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->Next = NULL;
    Device->Prev = NULL;
    Device->References = 1;

    // Check if AHCI is already initialized
    SAFE_USE(AHCIState.Base) {
        DEBUG(TEXT("[AHCIAttach] AHCI already initialized, skipping duplicate controller"));
        return Device; // Return heap-allocated device but don't reinitialize
    }

    // Store the PCI device for interrupt handling
    AHCIState.Device = Device;

    DEBUG(TEXT("[AHCIAttach] Attaching AHCI controller %x:%x.%u"),
          (INT)Device->Info.Bus, (INT)Device->Info.Dev, (INT)Device->Info.Func);

    // Get ABAR (AHCI Base Address Register) from BAR5
    U32 ABAR = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 5);
    if (ABAR == 0) {
        DEBUG(TEXT("[AHCIAttach] No ABAR found"));
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[AHCIAttach] ABAR at %p"), (LINEAR)ABAR);

    // Verify ABAR is in a reasonable range
    if (ABAR < 0x1000 || ABAR > 0xFFFFF000) {
        DEBUG(TEXT("[AHCIAttach] ABAR address %p is out of range"), (LINEAR)ABAR);
        KernelHeapFree(Device);
        return NULL;
    }

    // Map AHCI registers using MapIOMemory for MMIO
    // AHCI registers typically need 4KB (0x1000) of space
    LINEAR MappedABAR = MapIOMemory(ABAR, N_4KB);
    if (MappedABAR == 0) {
        DEBUG(TEXT("[AHCIAttach] Failed to map ABAR %p"), (LINEAR)ABAR);
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[AHCIAttach] ABAR mapped to virtual address 0x%X"), MappedABAR);
    AHCIState.Base = (LPAHCI_HBA_MEM)MappedABAR;

    // Enable bus mastering
    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 1);

    // Initialize AHCI
    if (InitializeAHCIController() != DF_ERROR_SUCCESS) {
        DEBUG(TEXT("[AHCIAttach] Failed to initialize AHCI controller"));
        KernelHeapFree(Device);
        return NULL;
    }

    return Device;
}

/***************************************************************************/

/**
 * @brief Initialize AHCI controller, ports, and interrupt handling.
 *
 * Maps HBA registers, enumerates implemented ports, and leaves interrupts
 * masked for polling mode.
 *
 * @return DF_ERROR_SUCCESS on success or an error code.
 */
static U32 InitializeAHCIController(void) {
    if (AHCIState.Base == NULL) {
        return DF_ERROR_BADPARAM;
    }

    DEBUG(TEXT("[InitializeAHCIController] Initializing AHCI HBA"));
    DEBUG(TEXT("[InitializeAHCIController] Base address: %p"), (LINEAR)AHCIState.Base);

    if (!AHCIState.InterruptRegistered) {
        AHCIRegisterInterrupts();
    }

    MemorySet(AHCIState.Ports, 0, sizeof(AHCIState.Ports));
    AHCIState.PendingPortsMask = 0;

    // Test read access to AHCI registers before proceeding
    volatile U32* testPtr = (volatile U32*)AHCIState.Base;
    DEBUG(TEXT("[InitializeAHCIController] Testing memory access..."));

    U32 testRead = *testPtr;
    DEBUG(TEXT("[InitializeAHCIController] First DWORD: 0x%X"), testRead);

    // Check AHCI version - offset 0x10
    volatile U32* versionPtr = (volatile U32*)((U8*)AHCIState.Base + 0x10);
    U32 version = *versionPtr;
    DEBUG(TEXT("[InitializeAHCIController] AHCI version %x.%x"),
          (version >> 16) & 0xFFFF, version & 0xFFFF);

    // Get capabilities
    U32 cap = AHCIState.Base->cap;
    U32 nports = (cap & AHCI_CAP_NP_MASK) + 1;

    DEBUG(TEXT("[InitializeAHCIController] %u ports, CAP=%x"), nports, cap);

    // Enable AHCI mode
    AHCIState.Base->ghc |= AHCI_GHC_AE;

    // Get ports implemented
    AHCIState.PortsImplemented = AHCIState.Base->pi;
    DEBUG(TEXT("[InitializeAHCIController] Ports implemented: %x"), AHCIState.PortsImplemented);

    // Initialize available ports
    for (U32 i = 0; i < nports && i < AHCI_MAX_PORTS; i++) {
        if (AHCIState.PortsImplemented & (1 << i)) {
            LPAHCI_PORT AHCIPort = NewAHCIPort();

            SAFE_USE(AHCIPort) {
                if (InitializeAHCIPort(AHCIPort, i)) {
                    ListAddItem(Kernel.Disk, AHCIPort);
                    DEBUG(TEXT("[InitializeAHCIController] Port %u added to disk list"), i);
                }
            }
        }
    }

    // Leave global interrupts masked. The disk driver uses polling for
    // command completion, so unmasking the HBA would generate useless INTx
    // storms on shared IRQ lines.
    AHCIState.Base->ghc &= ~AHCI_GHC_IE;
    AHCIState.InterruptEnabled = FALSE;

    DEBUG(TEXT("[InitializeAHCIController] AHCI initialization complete"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Issue an AHCI command (read/write) on a port.
 *
 * @param AHCIPort Target port.
 * @param Command ATA command byte.
 * @param LBA Logical block address.
 * @param SectorCount Number of sectors to transfer.
 * @param Buffer Data buffer.
 * @param IsWrite TRUE for write, FALSE for read.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 AHCICommand(LPAHCI_PORT AHCIPort, U8 Command, U32 LBA, U16 SectorCount, LPVOID Buffer, BOOL IsWrite) {
    if (AHCIPort == NULL || Buffer == NULL) {
        DEBUG(TEXT("[AHCICommand] Invalid parameters"));
        return DF_ERROR_BADPARAM;
    }

    // DEBUG(TEXT("[AHCICommand] Command=%x, LBA=%x, SectorCount=%u, IsWrite=%d"), Command, LBA, SectorCount, IsWrite);

    LPAHCI_HBA_PORT Port = AHCIPort->HBAPort;
    if (Port == NULL) {
        DEBUG(TEXT("[AHCICommand] Port not initialized"));
        return DF_ERROR_HARDWARE;
    }

    // Wait for port to be ready
    U32 timeout = 1000000;
    while ((Port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        DEBUG(TEXT("[AHCICommand] Port busy timeout"));
        return DF_ERROR_TIMEOUT;
    }

    // Clear pending interrupts
    Port->is = 0xFFFFFFFF;

    // Set up command header for slot 0
    LPAHCI_CMD_HEADER cmdheader = &AHCIPort->CommandList[0];
    cmdheader->cfl = sizeof(FIS_REG_H2D) / 4; // FIS length in DWORDs
    cmdheader->w = IsWrite ? 1 : 0;           // Write flag
    cmdheader->prdtl = 1;                     // One PRDT entry

    // Set up command table
    LPAHCI_CMD_TBL cmdtbl = AHCIPort->CommandTable;
    MemorySet(cmdtbl, 0, sizeof(AHCI_CMD_TBL));

    // Set up FIS
    FIS_REG_H2D *cmdfis = (FIS_REG_H2D*)cmdtbl->cfis;
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = Command;
    cmdfis->lba0 = LBA & 0xFF;
    cmdfis->lba1 = (LBA >> 8) & 0xFF;
    cmdfis->lba2 = (LBA >> 16) & 0xFF;
    cmdfis->device = 1 << 6; // LBA mode
    cmdfis->lba3 = (LBA >> 24) & 0xFF;
    cmdfis->lba4 = 0; // LBA 32-39 (we're using 32-bit LBA)
    cmdfis->lba5 = 0; // LBA 40-47
    cmdfis->countl = SectorCount & 0xFF;
    cmdfis->counth = (SectorCount >> 8) & 0xFF;

    // Set up PRDT entry
    PHYSICAL bufferPhys = MapLinearToPhysical((LINEAR)Buffer);
    if (bufferPhys == 0) {
        DEBUG(TEXT("[AHCICommand] Failed to get physical address for buffer"));
        return DF_ERROR_HARDWARE;
    }

    cmdtbl->prdt_entry[0].dba = bufferPhys;
    cmdtbl->prdt_entry[0].dbau = 0; // Assume 32-bit
    cmdtbl->prdt_entry[0].dbc = (SectorCount * 512) - 1; // Byte count - 1
    cmdtbl->prdt_entry[0].i = 0; // No interrupt on completion for this entry

    // DEBUG(TEXT("[AHCICommand] Buffer virt=0x%X phys=0x%X, size=%u bytes"), (U32)Buffer, bufferPhys, SectorCount * 512);

    // Issue command
    Port->ci = 1; // Issue command slot 0

    // Wait for completion
    timeout = 1000000;
    while ((Port->ci & 1) && timeout > 0) {
        if (Port->is & AHCI_PORT_IS_TFES) {
            DEBUG(TEXT("[AHCICommand] Task file error"));
            return DF_ERROR_HARDWARE;
        }
        timeout--;
    }

    if (timeout == 0) {
        DEBUG(TEXT("[AHCICommand] Command timeout"));
        return DF_ERROR_TIMEOUT;
    }

    // Check for errors
    if (Port->is & AHCI_PORT_IS_TFES) {
        DEBUG(TEXT("[AHCICommand] Task file error after completion"));
        return DF_ERROR_HARDWARE;
    }

    // DEBUG(TEXT("[AHCICommand] Command completed successfully"));
    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read sectors from a SATA disk using AHCI.
 *
 * @param Control IO control structure describing request.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 Read(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Current;
    U32 Result;

    // Check validity of parameters
    if (Control == NULL) return DF_ERROR_BADPARAM;

    // Get the physical disk to which operation applies
    AHCIPort = (LPAHCI_PORT)Control->Disk;
    if (AHCIPort == NULL) return DF_ERROR_BADPARAM;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_DISK) return DF_ERROR_BADPARAM;

    CacheCleanup(&AHCIPort->SectorCache, GetSystemTime());

    for (Current = 0; Current < Control->NumSectors; Current++) {
        SATA_CACHE_CONTEXT Context = {Control->SectorLow + Current, 0};
        LPSECTORBUFFER Buffer = (LPSECTORBUFFER)CacheFind(&AHCIPort->SectorCache, SATACacheMatcher, &Context);

        if (Buffer == NULL) {
            Buffer = (LPSECTORBUFFER)KernelHeapAlloc(sizeof(SECTORBUFFER));

            if (Buffer == NULL) return DF_ERROR_UNEXPECT;

            Buffer->SectorLow = Context.SectorLow;
            Buffer->SectorHigh = Context.SectorHigh;
            Buffer->Dirty = 0;

            Result = AHCICommand(
                AHCIPort, ATA_CMD_READ_DMA_EXT, Context.SectorLow, 1, Buffer->Data, FALSE);

            if (Result != DF_ERROR_SUCCESS) {
                KernelHeapFree(Buffer);
                return Result;
            }

            if (!CacheAdd(&AHCIPort->SectorCache, Buffer, DISK_CACHE_TTL_MS)) {
                KernelHeapFree(Buffer);
                return DF_ERROR_UNEXPECT;
            }
        }

        MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), Buffer->Data, SECTOR_SIZE);
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Write sectors to a SATA disk using AHCI.
 *
 * @param Control IO control structure describing request.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 Write(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Current;
    U32 Result;

    // Check validity of parameters
    if (Control == NULL) return DF_ERROR_BADPARAM;

    // Get the physical disk to which operation applies
    AHCIPort = (LPAHCI_PORT)Control->Disk;
    if (AHCIPort == NULL) return DF_ERROR_BADPARAM;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_DISK) return DF_ERROR_BADPARAM;

    // Check access permissions
    if (AHCIPort->Access & DISK_ACCESS_READONLY) return DF_ERROR_NOPERM;

    CacheCleanup(&AHCIPort->SectorCache, GetSystemTime());

    for (Current = 0; Current < Control->NumSectors; Current++) {
        SATA_CACHE_CONTEXT Context = {Control->SectorLow + Current, 0};
        LPSECTORBUFFER Buffer = (LPSECTORBUFFER)CacheFind(&AHCIPort->SectorCache, SATACacheMatcher, &Context);
        BOOL AddedToCache = FALSE;

        if (Buffer == NULL) {
            Buffer = (LPSECTORBUFFER)KernelHeapAlloc(sizeof(SECTORBUFFER));

            if (Buffer == NULL) return DF_ERROR_UNEXPECT;

            Buffer->SectorLow = Context.SectorLow;
            Buffer->SectorHigh = Context.SectorHigh;
            Buffer->Dirty = 0;
            AddedToCache = TRUE;
        }

        MemoryCopy(Buffer->Data, ((U8*)Control->Buffer) + (Current * SECTOR_SIZE), SECTOR_SIZE);
        Buffer->Dirty = 1;

        Result = AHCICommand(
            AHCIPort, ATA_CMD_WRITE_DMA_EXT, Context.SectorLow, 1, Buffer->Data, TRUE);

        if (Result != DF_ERROR_SUCCESS) {
            if (AddedToCache) {
                KernelHeapFree(Buffer);
            }
            return Result;
        }

        Buffer->Dirty = 0;

        if (AddedToCache) {
            if (!CacheAdd(&AHCIPort->SectorCache, Buffer, DISK_CACHE_TTL_MS)) {
                KernelHeapFree(Buffer);
                return DF_ERROR_UNEXPECT;
            }
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Retrieve disk information for a SATA device.
 *
 * @param Info Output structure to populate.
 * @return DF_ERROR_SUCCESS on success, DF_ERROR_BADPARAM otherwise.
 */
static U32 GetInfo(LPDISKINFO Info) {
    LPAHCI_PORT AHCIPort;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    // Get the physical disk to which operation applies
    AHCIPort = (LPAHCI_PORT)Info->Disk;
    if (AHCIPort == NULL) return DF_ERROR_BADPARAM;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_DISK) return DF_ERROR_BADPARAM;

    Info->Type = DRIVER_TYPE_HARDDISK;
    Info->Removable = 0;
    Info->NumSectors = AHCIPort->Geometry.Cylinders * AHCIPort->Geometry.Heads * AHCIPort->Geometry.SectorsPerTrack;
    Info->Access = AHCIPort->Access;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Set access parameters for a SATA disk.
 *
 * @param Access Access parameters to store.
 * @return DF_ERROR_SUCCESS on success, DF_ERROR_BADPARAM otherwise.
 */
static U32 SetAccess(LPDISKACCESS Access) {
    LPAHCI_PORT AHCIPort;

    if (Access == NULL) return DF_ERROR_BADPARAM;

    // Get the physical disk to which operation applies
    AHCIPort = (LPAHCI_PORT)Access->Disk;
    if (AHCIPort == NULL) return DF_ERROR_BADPARAM;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_DISK) return DF_ERROR_BADPARAM;

    AHCIPort->Access = Access->Access;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Register AHCI interrupt handlers and enable HBA IRQs if possible.
 *
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL AHCIRegisterInterrupts(void) {
    LPPCI_DEVICE Device = AHCIState.Device;

    if (Device == NULL) {
        DEBUG(TEXT("[AHCIRegisterInterrupts] No PCI device context available"));
        return FALSE;
    }

    if (AHCIState.InterruptRegistered) {
        return TRUE;
    }

    U8 LegacyIRQ = Device->Info.IRQLine;
    if (LegacyIRQ == 0xFFU) {
        WARNING(TEXT("[AHCIRegisterInterrupts] Controller reports no legacy IRQ line"));
        return FALSE;
    }

    BOOL Registered = FALSE;

    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        DEVICE_INTERRUPT_REGISTRATION Registration = {
            .Device = (LPDEVICE)Device,
            .LegacyIRQ = LegacyIRQ,
            .TargetCPU = 0,
            .InterruptHandler = AHCIInterruptTopHalf,
            .DeferredCallback = AHCIInterruptBottomHalf,
            .PollCallback = AHCIInterruptPoll,
            .Context = (LPVOID)&AHCIState,
            .Name = Device->Driver ? Device->Driver->Product : TEXT("AHCI"),
        };

        if (DeviceInterruptRegister(&Registration, &AHCIState.InterruptSlot)) {
            AHCIState.InterruptRegistered = TRUE;
            AHCIState.InterruptEnabled = DeviceInterruptSlotIsEnabled(AHCIState.InterruptSlot);
            AHCIState.PendingPortsMask = 0;
            DEBUG(TEXT("[AHCIRegisterInterrupts] Slot %u registered for IRQ %u (mode=%s)"),
                  AHCIState.InterruptSlot,
                  LegacyIRQ,
                  AHCIState.InterruptEnabled ? TEXT("INTERRUPT") : TEXT("POLLING"));
            Registered = TRUE;
        } else {
            WARNING(TEXT("[AHCIRegisterInterrupts] Failed to register interrupt slot for IRQ %u"), LegacyIRQ);
            AHCIState.InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        }
    }

    return Registered;
}

/***************************************************************************/

/**
 * @brief Top-half interrupt handler for AHCI.
 *
 * Acknowledges interrupts, records pending ports, and defers work.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 * @return TRUE if handled.
 */
static BOOL AHCIInterruptTopHalf(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    LPAHCI_STATE State = (LPAHCI_STATE)Context;
    if (State == NULL || State->Base == NULL) {
        return FALSE;
    }

    U32 GlobalStatus = State->Base->is;
    if (GlobalStatus == 0U) {
        return FALSE;
    }

    State->Base->is = GlobalStatus;

    BOOL ShouldSignal = FALSE;
    for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
        if ((GlobalStatus & (1U << PortIndex)) == 0U) {
            continue;
        }

        LPAHCI_HBA_PORT HwPort = &State->Base->ports[PortIndex];
        U32 PortStatus = HwPort->is;
        HwPort->is = PortStatus;

        if (PortStatus == 0U) {
            continue;
        }

        LPAHCI_PORT Port = State->Ports[PortIndex];
        if (Port == NULL) {
            continue;
        }

        Port->PendingInterrupts |= PortStatus;
        State->PendingPortsMask |= (1U << PortIndex);
        ShouldSignal = TRUE;
    }

    if (!ShouldSignal) {
        static U32 SpuriousCount = 0;
        if (SpuriousCount < 4U) {
            DEBUG(TEXT("[AHCIInterruptTopHalf] Spurious global status %x"), GlobalStatus);
        }
        SpuriousCount++;
    }

    return ShouldSignal;
}

/***************************************************************************/

/**
 * @brief Bottom-half handler for AHCI interrupts.
 *
 * Clears per-port interrupts that were latched in the top half.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 */
static void AHCIInterruptBottomHalf(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    LPAHCI_STATE State = (LPAHCI_STATE)Context;
    if (State == NULL) {
        return;
    }

    U32 LocalMask;
    U32 LocalStatus[AHCI_MAX_PORTS];
    MemorySet(LocalStatus, 0, sizeof(LocalStatus));

    {
        U32 Flags;
        SaveFlags(&Flags);
        DisableInterrupts();
        LocalMask = State->PendingPortsMask;
        if (LocalMask != 0U) {
            for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
                LPAHCI_PORT Port = State->Ports[PortIndex];
                if (Port != NULL) {
                    LocalStatus[PortIndex] = (U32)Port->PendingInterrupts;
                    Port->PendingInterrupts = 0;
                }
            }
            State->PendingPortsMask = 0;
        }
        RestoreFlags(&Flags);
    }

    if (LocalMask == 0U) {
        return;
    }

    static U32 BottomHalfLogCount = 0;

    for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
        if ((LocalMask & (1U << PortIndex)) == 0U) {
            continue;
        }

        U32 PortStatus = LocalStatus[PortIndex];
        LPAHCI_PORT Port = State->Ports[PortIndex];
        if (Port == NULL || PortStatus == 0U) {
            continue;
        }

        SAFE_USE_VALID_ID((LPLISTNODE)Port, KOID_DISK) {
            if ((PortStatus & (1U << 30)) != 0U) {
                WARNING(TEXT("[AHCIInterruptBottomHalf] Port %u reported task file error (status=%x)"),
                        PortIndex,
                        PortStatus);
            } else if (BottomHalfLogCount < 4U) {
                DEBUG(TEXT("[AHCIInterruptBottomHalf] Port %u interrupt status %x"), PortIndex, PortStatus);
            }
        }

        BottomHalfLogCount++;
    }
}

/***************************************************************************/

/**
 * @brief Poll-mode handler to process pending AHCI interrupts.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 */
static void AHCIInterruptPoll(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    if (AHCIInterruptTopHalf(Device, Context)) {
        AHCIInterruptBottomHalf(Device, Context);
    }
}

/***************************************************************************/

BOOL AHCIIsInitialized(void) {
    return (AHCIState.Base != NULL);
}

/***************************************************************************/

void AHCIInterruptHandler(void) {
    if (AHCIState.Device == NULL || AHCIState.Base == NULL) {
        return;
    }

    if (AHCIInterruptTopHalf((LPDEVICE)AHCIState.Device, (LPVOID)&AHCIState)) {
        AHCIInterruptBottomHalf((LPDEVICE)AHCIState.Device, (LPVOID)&AHCIState);
    }
}

/***************************************************************************/

/**
 * @brief SATA driver command dispatcher.
 *
 * Handles load/unload, version, disk I/O, and access configuration requests.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver-specific status/value.
 */
UINT SATADiskCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((SATADiskDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_ERROR_SUCCESS;
            }

            SATADiskDriver.Flags |= DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;
        case DF_UNLOAD:
            if ((SATADiskDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_ERROR_SUCCESS;
            }

            SATADiskDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_DISK_RESET:
            return DF_ERROR_NOTIMPL;
        case DF_DISK_READ:
            return Read((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return Write((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return GetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return SetAccess((LPDISKACCESS)Parameter);
    }

    return DF_ERROR_NOTIMPL;
}
