
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


    ATA

\************************************************************************/

#include "../include/ATA.h"

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/InterruptController.h"

/***************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/

U32 ATADiskCommands(U32, U32);

DRIVER ATADiskDriver = {
    .ID = ID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_HARDDISK,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "IBM PC and compatibles",
    .Product = "ATA Disk Controller",
    .Command = ATADiskCommands};

/***************************************************************************/

// ATA physical disk, derives from PHYSICALDISK

typedef struct tag_ATADISK {
    PHYSICALDISK Header;
    DISKGEOMETRY Geometry;
    U32 Access;  // Access parameters
    U32 IOPort;  // 0x01F0 or 0x0170
    U32 IRQ;     // 0x0E
    U32 Drive;   // 0 or 1
    U32 NumBuffers;
    LPSECTORBUFFER Buffer;
} ATADISK, *LPATADISK;

/***************************************************************************/

static LPATADISK NewATADisk(void) {
    LPATADISK This;

    This = (LPATADISK)KernelHeapAlloc(sizeof(ATADISK));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(ATADISK));

    This->Header.ID = ID_DISK;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &ATADiskDriver;
    This->Access = 0;

    return This;
}

/***************************************************************************/

static BOOL ATAWaitNotBusy(U32 Port, U32 TimeOut) {
    U32 Status;

    while (TimeOut--) {
        Status = InPortByte(Port + HD_STATUS);
        if ((Status & (HD_STATUS_BUSY | HD_STATUS_READY)) == HD_STATUS_READY) {
            return TRUE;
        }
    }

    WARNING(TEXT("[ATAWaitNotBusy] Time-out in ATA port %x"), Port);

    return FALSE;
}

/***************************************************************************/

static U32 InitializeATA(void) {
    LPATADISK Disk;
    LPATADRIVEID ATAID;
    U8 Buffer[SECTOR_SIZE];
    U32 Port;
    U32 Drive;
    U32 Index;
    U32 DisksFound = 0;

    DEBUG(TEXT("[InitializeATA] Enter"));

    DisableInterrupt(IRQ_ATA);

    //-------------------------------------
    // Identify the drives

    for (Port = 0; Port < 2; Port++) {
        U32 RealPort = 0;

        if (Port == 0) RealPort = ATA_PORT_0;
        if (Port == 1) RealPort = ATA_PORT_1;

        for (Drive = 0; Drive < 2; Drive++) {
            if (ATAWaitNotBusy(RealPort, TIMEOUT) == FALSE) continue;

            OutPortByte(RealPort + HD_CYLINDERLOW, 0);
            OutPortByte(RealPort + HD_CYLINDERHIGH, 0);
            OutPortByte(RealPort + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4));

            // Add delay for drive selection
            for (U32 DelayIndex = 0; DelayIndex < 1000; DelayIndex++);

            // Check for floating bus (no drive present)
            U32 Status = InPortByte(RealPort + HD_STATUS);
            if (Status == 0xFF) continue;  // No drive present

            OutPortByte(RealPort + HD_SECTOR, 0);
            OutPortByte(RealPort + HD_NUMSECTORS, 1);
            OutPortByte(RealPort + HD_COMMAND, HD_COMMAND_IDENTIFY);

            if (ATAWaitNotBusy(RealPort, TIMEOUT) == FALSE) continue;

            // Check for error after IDENTIFY command
            Status = InPortByte(RealPort + HD_STATUS);
            if (Status & HD_STATUS_ERROR) continue;
            if (!(Status & HD_STATUS_DRQ)) continue;

            InPortStringWord(RealPort + HD_DATA, Buffer, SECTOR_SIZE / 2);

            ATAID = (LPATADRIVEID)Buffer;

            if (ATAID->PhysicalCylinders != 0 && ATAID->PhysicalHeads != 0 && ATAID->PhysicalSectors != 0) {
                DEBUG(TEXT("[InitializeATA] port: %x, drive: %x"), (U32)RealPort, (U32)Drive);

                Disk = NewATADisk();
                if (Disk == NULL) continue;

                Disk->Geometry.Cylinders = ATAID->PhysicalCylinders;
                Disk->Geometry.Heads = ATAID->PhysicalHeads;
                Disk->Geometry.SectorsPerTrack = ATAID->PhysicalSectors;
                Disk->Geometry.BytesPerSector = SECTOR_SIZE;
                Disk->IOPort = RealPort;
                Disk->IRQ = IRQ_ATA;
                Disk->Drive = Drive;
                Disk->NumBuffers = NUM_BUFFERS;
                Disk->Buffer = KernelHeapAlloc(NUM_BUFFERS * sizeof(SECTORBUFFER));

                //-------------------------------------
                // Clear the buffers

                for (Index = 0; Index < NUM_BUFFERS; Index++) {
                    Disk->Buffer[Index].SectorLow = MAX_U32;
                    Disk->Buffer[Index].SectorHigh = MAX_U32;
                    Disk->Buffer[Index].Score = 0;
                    Disk->Buffer[Index].Dirty = 0;
                }

                ListAddItem(Kernel.Disk, Disk);
                DisksFound++;
            }
        }
    }

    // Only enable IRQ if we found at least one disk
    if (DisksFound > 0) {
        EnableInterrupt(IRQ_ATA);
        DEBUG(TEXT("[InitializeATA] Found %d disk(s), IRQ enabled"), DisksFound);
    } else {
        DEBUG(TEXT("[InitializeATA] No disks found, IRQ remains disabled"));
    }

    DEBUG(TEXT("[InitializeATA] Exit"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/*
static U32 ControllerBusy(U32 Port) {
    U32 TimeOut = 100000;
    U32 Status;

    do {
        Status = InPortByte(Port + HD_STATUS);
    } while ((Status & HD_STATUS_BUSY) && TimeOut--);

    return Status;
}
*/

/***************************************************************************/

/*
static BOOL IsStatusOk(U32 Port) {
    U32 Status = InPortByte(Port + HD_STATUS);

    if (Status & HD_STATUS_BUSY) return TRUE;
    if (Status & HD_STATUS_WERROR) return FALSE;
    if (!(Status & HD_STATUS_READY)) return FALSE;
    if (!(Status & HD_STATUS_SEEK)) return FALSE;

    return TRUE;
}
*/

/***************************************************************************/

/*
static BOOL IsControllerReady(U32 Port, U32 Drive, U32 Head) {
    U32 Retry = 100;

    do {
        if (ControllerBusy(Port) & HD_STATUS_BUSY) return FALSE;
        OutPortByte(Port + HD_HEAD, 0xA0 | (Drive << 4) | Head);
        if (IsStatusOk(Port)) return TRUE;
    } while (Retry--);

    return FALSE;
}
*/

/***************************************************************************/

/*
static void ResetController(U32 Port) {
    UNUSED(Port);

    U32 Index;

    OutPortByte(HD_ALTCOMMAND, 4);
    for (Index = 0; Index < 1000; Index++) barrier();
    OutPortByte(HD_ALTCOMMAND, hd_info[0].ctl);
    for (Index = 0; Index < 1000; Index++) barrier();
    if (IsDriveBusy())
    {
      KernelLogText(LOG_VERBOSE, "HD : Controller still busy\n");
    }
    else
    if ((HD_Error = InPortByte(Port + HD_ERROR)) != 1)
    {
      KernelLogText(LOG_VERBOSE, "HD : Controller reset failed\n");
    }
}
*/

/***************************************************************************/

static void ATADriveOut(U32 Port, U32 Drive, U32 Command, U8* Buffer, U32 Cylinder, U32 Head, U32 Sector, U32 Count) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    if (ATAWaitNotBusy(Port, TIMEOUT) == FALSE) goto Out;

    OutPortByte(Port + HD_CYLINDERLOW, Cylinder & 0xFF);
    OutPortByte(Port + HD_CYLINDERHIGH, (Cylinder >> 8) & 0xFF);
    OutPortByte(Port + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4) | (Head & 0x0F));
    OutPortByte(Port + HD_SECTOR, Sector & 0xFF);
    OutPortByte(Port + HD_NUMSECTORS, Count & 0xFF);
    OutPortByte(Port + HD_COMMAND, Command);

    if (ATAWaitNotBusy(Port, TIMEOUT) == FALSE) goto Out;

    if (Command == HD_COMMAND_READ) {
        InPortStringWord(Port + HD_DATA, Buffer, (Count * SECTOR_SIZE) / 2);
    } else if (Command == HD_COMMAND_WRITE) {
        OutPortStringWord(Port + HD_DATA, Buffer, (Count * SECTOR_SIZE) / 2);
    }

Out:

    RestoreFlags(&Flags);
}

/***************************************************************************/

static U32 Read(LPIOCONTROL Control) {
    LPATADISK Disk;
    BLOCKPARAMS Params;
    U32 Index;
    U32 Current;

    //-------------------------------------
    // Check validity of parameters

    if (Control == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Control->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    for (Current = 0; Current < Control->NumSectors; Current++) {
        //-------------------------------------
        // Let's see if we already have this sector

        Index = FindSectorInBuffers(Disk->Buffer, Disk->NumBuffers, Control->SectorLow + Current, 0);

        if (Index != MAX_U32) {
            MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), Disk->Buffer[Index].Data, SECTOR_SIZE);
        } else {
            //-------------------------------------
            // Get a new buffer in which to read the sector

            Index = GetEmptyBuffer(Disk->Buffer, Disk->NumBuffers);

            if (Index == MAX_U32) return DF_ERROR_UNEXPECT;

            //-------------------------------------
            // We must now do a physical disk access

            DisableInterrupt(Disk->IRQ);

            SectorToBlockParams(&(Disk->Geometry), Control->SectorLow + Current, &Params);

            ATADriveOut(
                Disk->IOPort, Disk->Drive, HD_COMMAND_READ, Disk->Buffer[Index].Data, Params.Cylinder, Params.Head,
                Params.Sector, 1);

            EnableInterrupt(Disk->IRQ);

            //-------------------------------------
            // Update the buffer

            Disk->Buffer[Index].SectorLow = Control->SectorLow + Current;
            Disk->Buffer[Index].SectorHigh = 0;
            Disk->Buffer[Index].Score = 10;

            //-------------------------------------
            // Copy the buffer to the user's buffer

            MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), Disk->Buffer[Index].Data, SECTOR_SIZE);
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 Write(LPIOCONTROL Control) {
    LPATADISK Disk;
    BLOCKPARAMS Params;
    U32 Index;
    U32 Current;

    //-------------------------------------
    // Check validity of parameters

    if (Control == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Control->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check access permissions

    if (Disk->Access & DISK_ACCESS_READONLY) return DF_ERROR_NOPERM;

    for (Current = 0; Current < Control->NumSectors; Current++) {
        //-------------------------------------
        // Get a buffer for this sector (or use existing one)

        Index = FindSectorInBuffers(Disk->Buffer, Disk->NumBuffers, Control->SectorLow + Current, 0);

        if (Index == MAX_U32) {
            Index = GetEmptyBuffer(Disk->Buffer, Disk->NumBuffers);
            if (Index == MAX_U32) return DF_ERROR_UNEXPECT;
        }

        //-------------------------------------
        // Update buffer with new data

        MemoryCopy(Disk->Buffer[Index].Data, ((U8*)Control->Buffer) + (Current * SECTOR_SIZE), SECTOR_SIZE);
        Disk->Buffer[Index].SectorLow = Control->SectorLow + Current;
        Disk->Buffer[Index].SectorHigh = 0;
        Disk->Buffer[Index].Score = 10;
        Disk->Buffer[Index].Dirty = 1;

        //-------------------------------------
        // Write to physical disk

        DisableInterrupt(Disk->IRQ);

        SectorToBlockParams(&(Disk->Geometry), Control->SectorLow + Current, &Params);

        ATADriveOut(
            Disk->IOPort, Disk->Drive, HD_COMMAND_WRITE, Disk->Buffer[Index].Data, Params.Cylinder, Params.Head,
            Params.Sector, 1);

        EnableInterrupt(Disk->IRQ);

        //-------------------------------------
        // Mark buffer as clean after successful write

        Disk->Buffer[Index].Dirty = 0;
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 GetInfo(LPDISKINFO Info) {
    LPATADISK Disk;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Info->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    //-------------------------------------

    Info->Type = DRIVER_TYPE_HARDDISK;
    Info->Removable = 0;
    Info->NumSectors = Disk->Geometry.Cylinders * Disk->Geometry.Heads * Disk->Geometry.SectorsPerTrack;
    Info->Access = Disk->Access;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 SetAccess(LPDISKACCESS Access) {
    LPATADISK Disk;

    if (Access == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Access->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    Disk->Access = Access->Access;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

void HardDriveHandler(void) {
    static U32 Busy = 0;
    U32 Status0, Status1;
    BOOL RealInterrupt = FALSE;

    if (Busy) return;
    Busy = 1;

    // Check if this is a real ATA interrupt by reading status registers
    Status0 = InPortByte(ATA_PORT_0 + HD_STATUS);
    Status1 = InPortByte(ATA_PORT_1 + HD_STATUS);

    // A real ATA interrupt should have specific status bits set
    // and should not return 0xFF (floating bus)
    if (Status0 != 0xFF && (Status0 & (HD_STATUS_DRQ | HD_STATUS_ERROR))) {
        RealInterrupt = TRUE;
        DEBUG(TEXT("[HardDriveHandler] Real interrupt on primary channel, status: %x"), Status0);
    }

    if (Status1 != 0xFF && (Status1 & (HD_STATUS_DRQ | HD_STATUS_ERROR))) {
        RealInterrupt = TRUE;
        DEBUG(TEXT("[HardDriveHandler] Real interrupt on secondary channel, status: %x"), Status1);
    }

    // Only process if this is a real interrupt
    if (RealInterrupt) {
        // TODO: Add proper interrupt handling code here
        DEBUG(TEXT("[HardDriveHandler] Processing ATA interrupt"));
    }

    Busy = 0;
}

/***************************************************************************/

U32 ATADiskCommands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return InitializeATA();
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
