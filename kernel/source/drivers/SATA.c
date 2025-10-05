
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

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/PCI.h"
#include "User.h"

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

U32 SATADiskCommands(U32, U32);

DRIVER SATADiskDriver = {
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
    U32 NumBuffers;
    LPSECTORBUFFER Buffer;
} AHCI_PORT, *LPAHCI_PORT;

/***************************************************************************/
// Global AHCI state

typedef struct tag_AHCI_STATE {
    LPAHCI_HBA_MEM Base;
    U32 PortsImplemented;
    LPPCI_DEVICE Device;
} AHCI_STATE, *LPAHCI_STATE;

static AHCI_STATE AHCIState = {NULL, 0, NULL};

/***************************************************************************/
// AHCI PCI Driver

static U32 AHCIProbe(U32 Function, U32 Parameter);
static LPPCI_DEVICE AHCIAttach(LPPCI_DEVICE PciDevice);
static U32 InitializeAHCIController(void);

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
    This->NumBuffers = NUM_BUFFERS;

    return This;
}

/***************************************************************************/

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

static BOOL InitializeAHCIPort(LPAHCI_PORT AHCIPort, U32 PortNum) {
    LPAHCI_HBA_PORT Port = &AHCIState.Base->ports[PortNum];

    DEBUG(TEXT("[InitializeAHCIPort] Initializing port %u"), PortNum);

    // Check if port is implemented
    if ((AHCIState.PortsImplemented & (1 << PortNum)) == 0) {
        return FALSE;
    }

    // Check if device is present
    U32 ssts = Port->ssts;
    U32 det = ssts & AHCI_PORT_SSTS_DET_MASK;
    DEBUG(TEXT("[InitializeAHCIPort] Port %u SSTS: 0x%08X, DET: 0x%X"), PortNum, ssts, det);
    DEBUG(TEXT("[InitializeAHCIPort] Expected DET_ESTABLISHED: 0x%X"), AHCI_PORT_SSTS_DET_ESTABLISHED);

    if (det == AHCI_PORT_SSTS_DET_NONE) {
        DEBUG(TEXT("[InitializeAHCIPort] No device on port %u (DET=0x%X)"), PortNum, det);
        return FALSE;
    }

    if (det == AHCI_PORT_SSTS_DET_PRESENT) {
        DEBUG(TEXT("[InitializeAHCIPort] Device present on port %u but communication not established, continuing..."), PortNum);
    } else if (det == AHCI_PORT_SSTS_DET_ESTABLISHED) {
        DEBUG(TEXT("[InitializeAHCIPort] Device communication established on port %u"), PortNum);
    } else {
        DEBUG(TEXT("[InitializeAHCIPort] Unknown DET state 0x%X on port %u"), det, PortNum);
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

    // Allocate sector buffers
    AHCIPort->Buffer = KernelHeapAlloc(NUM_BUFFERS * sizeof(SECTORBUFFER));
    if (AHCIPort->Buffer == NULL) {
        DEBUG(TEXT("[InitializeAHCIPort] Failed to allocate sector buffers"));
        return FALSE;
    }

    // Initialize sector buffers
    for (U32 i = 0; i < NUM_BUFFERS; i++) {
        AHCIPort->Buffer[i].SectorLow = MAX_U32;
        AHCIPort->Buffer[i].SectorHigh = MAX_U32;
        AHCIPort->Buffer[i].Score = 0;
        AHCIPort->Buffer[i].Dirty = 0;
    }

    // Set up port registers with physical addresses for DMA
    PHYSICAL CommandListPhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandList);
    PHYSICAL FISBasePhys = MapLinearToPhysical((LINEAR)AHCIPort->FISBase);
    PHYSICAL CommandTablePhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandTable);

    Port->clb = CommandListPhys;
    Port->clbu = 0; // Assume 32-bit system
    Port->fb = FISBasePhys;
    Port->fbu = 0; // Assume 32-bit system

    DEBUG(TEXT("[InitializeAHCIPort] CommandList: virt=0x%X phys=0x%X"),
          (U32)AHCIPort->CommandList, CommandListPhys);
    DEBUG(TEXT("[InitializeAHCIPort] FISBase: virt=0x%X phys=0x%X"),
          (U32)AHCIPort->FISBase, FISBasePhys);

    // Set up command header for slot 0
    AHCIPort->CommandList[0].cfl = sizeof(FIS_REG_H2D) / 4; // FIS length
    AHCIPort->CommandList[0].prdtl = 1; // One PRDT entry
    AHCIPort->CommandList[0].ctba = CommandTablePhys;
    AHCIPort->CommandList[0].ctbau = 0;

    // Store references
    AHCIPort->PortNumber = PortNum;
    AHCIPort->HBAPort = Port;
    AHCIPort->HBAMem = AHCIState.Base;

    // Clear interrupt status
    Port->is = 0xFFFFFFFF;

    // Enable interrupts
    Port->ie = 0xFFFFFFFF;

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

static U32 AHCIProbe(U32 Function, U32 Parameter) {
    LPPCI_INFO PciInfo = (LPPCI_INFO)Parameter;

    if (Function != DF_PROBE) {
        return DF_ERROR_NOTIMPL;
    }

    if (PciInfo == NULL) {
        return DF_ERROR_BADPARAM;
    }

    DEBUG(TEXT("[AHCIProbe] Found AHCI controller %X:%X"),
          (U32)PciInfo->VendorID, (U32)PciInfo->DeviceID);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

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

    DEBUG(TEXT("[AHCIAttach] Attaching AHCI controller %X:%X.%u"),
          (U32)Device->Info.Bus, (U32)Device->Info.Dev, (U32)Device->Info.Func);

    // Get ABAR (AHCI Base Address Register) from BAR5
    U32 ABAR = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 5);
    if (ABAR == 0) {
        DEBUG(TEXT("[AHCIAttach] No ABAR found"));
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("[AHCIAttach] ABAR at 0x%X"), ABAR);

    // Verify ABAR is in a reasonable range
    if (ABAR < 0x1000 || ABAR > 0xFFFFF000) {
        DEBUG(TEXT("[AHCIAttach] ABAR address 0x%X is out of range"), ABAR);
        KernelHeapFree(Device);
        return NULL;
    }

    // Map AHCI registers using MapIOMemory for MMIO
    // AHCI registers typically need 4KB (0x1000) of space
    LINEAR MappedABAR = MapIOMemory(ABAR, 0x1000);
    if (MappedABAR == 0) {
        DEBUG(TEXT("[AHCIAttach] Failed to map ABAR 0x%X"), ABAR);
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

static U32 InitializeAHCIController(void) {
    if (AHCIState.Base == NULL) {
        return DF_ERROR_BADPARAM;
    }

    DEBUG(TEXT("[InitializeAHCIController] Initializing AHCI HBA"));
    DEBUG(TEXT("[InitializeAHCIController] Base address: 0x%X"), (U32)AHCIState.Base);

    // Test read access to AHCI registers before proceeding
    volatile U32* testPtr = (volatile U32*)AHCIState.Base;
    DEBUG(TEXT("[InitializeAHCIController] Testing memory access..."));

    U32 testRead = *testPtr;
    DEBUG(TEXT("[InitializeAHCIController] First DWORD: 0x%X"), testRead);

    // Check AHCI version - offset 0x10
    volatile U32* versionPtr = (volatile U32*)((U8*)AHCIState.Base + 0x10);
    U32 version = *versionPtr;
    DEBUG(TEXT("[InitializeAHCIController] AHCI version %X.%X"),
          (version >> 16) & 0xFFFF, version & 0xFFFF);

    // Get capabilities
    U32 cap = AHCIState.Base->cap;
    U32 nports = (cap & AHCI_CAP_NP_MASK) + 1;

    DEBUG(TEXT("[InitializeAHCIController] %u ports, CAP=0x%X"), nports, cap);

    // Enable AHCI mode
    AHCIState.Base->ghc |= AHCI_GHC_AE;

    // Get ports implemented
    AHCIState.PortsImplemented = AHCIState.Base->pi;
    DEBUG(TEXT("[InitializeAHCIController] Ports implemented: 0x%X"), AHCIState.PortsImplemented);

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

    // Enable global interrupts
    AHCIState.Base->ghc |= AHCI_GHC_IE;

    DEBUG(TEXT("[InitializeAHCIController] AHCI initialization complete"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 AHCICommand(LPAHCI_PORT AHCIPort, U8 Command, U32 LBA, U16 SectorCount, LPVOID Buffer, BOOL IsWrite) {
    if (AHCIPort == NULL || Buffer == NULL) {
        DEBUG(TEXT("[AHCICommand] Invalid parameters"));
        return DF_ERROR_BADPARAM;
    }

    // DEBUG(TEXT("[AHCICommand] Command=0x%02X, LBA=0x%08X, SectorCount=%u, IsWrite=%d"), Command, LBA, SectorCount, IsWrite);

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

static U32 Read(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Index;
    U32 Current;
    U32 Result;

    // Check validity of parameters
    if (Control == NULL) return DF_ERROR_BADPARAM;

    // Get the physical disk to which operation applies
    AHCIPort = (LPAHCI_PORT)Control->Disk;
    if (AHCIPort == NULL) return DF_ERROR_BADPARAM;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_DISK) return DF_ERROR_BADPARAM;

    for (Current = 0; Current < Control->NumSectors; Current++) {
        // Let's see if we already have this sector
        Index = FindSectorInBuffers(AHCIPort->Buffer, AHCIPort->NumBuffers, Control->SectorLow + Current, 0);

        if (Index != MAX_U32) {
            MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), AHCIPort->Buffer[Index].Data, SECTOR_SIZE);
        } else {
            // Get a new buffer in which to read the sector
            Index = GetEmptyBuffer(AHCIPort->Buffer, AHCIPort->NumBuffers);

            if (Index == MAX_U32) return DF_ERROR_UNEXPECT;

            // Read from AHCI
            Result = AHCICommand(AHCIPort, ATA_CMD_READ_DMA_EXT, Control->SectorLow + Current, 1,
                                AHCIPort->Buffer[Index].Data, FALSE);

            if (Result != DF_ERROR_SUCCESS) {
                return Result;
            }

            // Update the buffer
            AHCIPort->Buffer[Index].SectorLow = Control->SectorLow + Current;
            AHCIPort->Buffer[Index].SectorHigh = 0;
            AHCIPort->Buffer[Index].Score = 10;

            // Copy the buffer to the user's buffer
            MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), AHCIPort->Buffer[Index].Data, SECTOR_SIZE);
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 Write(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Index;
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

    for (Current = 0; Current < Control->NumSectors; Current++) {
        // Get a buffer for this sector (or use existing one)
        Index = FindSectorInBuffers(AHCIPort->Buffer, AHCIPort->NumBuffers, Control->SectorLow + Current, 0);

        if (Index == MAX_U32) {
            Index = GetEmptyBuffer(AHCIPort->Buffer, AHCIPort->NumBuffers);
            if (Index == MAX_U32) return DF_ERROR_UNEXPECT;
        }

        // Update buffer with new data
        MemoryCopy(AHCIPort->Buffer[Index].Data, ((U8*)Control->Buffer) + (Current * SECTOR_SIZE), SECTOR_SIZE);
        AHCIPort->Buffer[Index].SectorLow = Control->SectorLow + Current;
        AHCIPort->Buffer[Index].SectorHigh = 0;
        AHCIPort->Buffer[Index].Score = 10;
        AHCIPort->Buffer[Index].Dirty = 1;

        // Write to AHCI
        Result = AHCICommand(AHCIPort, ATA_CMD_WRITE_DMA_EXT, Control->SectorLow + Current, 1,
                           AHCIPort->Buffer[Index].Data, TRUE);

        if (Result != DF_ERROR_SUCCESS) {
            return Result;
        }

        // Mark buffer as clean after successful write
        AHCIPort->Buffer[Index].Dirty = 0;
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

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

BOOL AHCIIsInitialized(void) {
    return (AHCIState.Base != NULL);
}

/***************************************************************************/

void AHCIInterruptHandler(void) {
    if (AHCIState.Base == NULL) return;

    U32 is = AHCIState.Base->is;

    // Handle per-port interrupts
    for (U32 i = 0; i < AHCI_MAX_PORTS; i++) {
        if (is & (1 << i)) {
            LPAHCI_HBA_PORT Port = &AHCIState.Base->ports[i];
            U32 port_is = Port->is;

            // Clear port interrupt
            Port->is = port_is;

            // Handle specific interrupt types
            if (port_is & (1 << 30)) { // Task file error
                DEBUG(TEXT("[AHCIInterruptHandler] Task file error on port %u"), i);
            }

            if (port_is & (1 << 0)) { // Device to Host Register FIS Interrupt
                // Command completed
            }
        }
    }

    // Clear global interrupt
    AHCIState.Base->is = is;
}

/***************************************************************************/

U32 SATADiskCommands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return DF_ERROR_SUCCESS;
        case DF_UNLOAD:
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
