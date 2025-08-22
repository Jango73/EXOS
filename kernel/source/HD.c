
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/HD.h"

#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/

U32 StdHardDiskCommands(U32, U32);

DRIVER StdHardDiskDriver = {
    ID_DRIVER,
    1,
    NULL,
    NULL,
    DRIVER_TYPE_HARDDISK,
    VER_MAJOR,
    VER_MINOR,
    "Jango73",
    "IBM PC and compatibles",
    "Standard Hard Disk Controller",
    StdHardDiskCommands};

/***************************************************************************/

#define MAX_HD 4
#define TIMEOUT 100000
#define NUM_BUFFERS 32

/***************************************************************************/
// Disk sector buffer

typedef struct tag_SECTORBUFFER {
    U32 SectorLow;
    U32 SectorHigh;
    U32 Score;
    U32 Dirty;
    U8 Data[SECTOR_SIZE];
} SECTORBUFFER, *LPSECTORBUFFER;

/***************************************************************************/
// Standard physical disk, derives from PHYSICALDISK

typedef struct tag_STDHARDDISK {
    PHYSICALDISK Header;
    DISKGEOMETRY Geometry;
    U32 Access;  // Access parameters
    U32 IOPort;  // 0x01F0 or 0x0170
    U32 IRQ;     // 0x0E
    U32 Drive;   // 0 or 1
    U32 NumBuffers;
    LPSECTORBUFFER Buffer;
} STDHARDDISK, *LPSTDHARDDISK;

/***************************************************************************/

typedef struct tag_BLOCKPARAMS {
    U32 Cylinder;
    U32 Head;
    U32 Sector;
} BLOCKPARAMS, *LPBLOCKPARAMS;

/***************************************************************************/

static LPSTDHARDDISK NewStdHardDisk(void) {
    LPSTDHARDDISK This;

    This = (LPSTDHARDDISK)KernelMemAlloc(sizeof(STDHARDDISK));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(STDHARDDISK));

    This->Header.ID = ID_DISK;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &StdHardDiskDriver;
    This->Access = 0;

    return This;
}

/***************************************************************************/

void SectorToBlockParams(LPDISKGEOMETRY Geometry, U32 Sector, LPBLOCKPARAMS Block) {
    U32 Temp1, Temp2;

    Block->Cylinder = 0;
    Block->Head = 0;
    Block->Sector = 0;

    if (Geometry->Heads == 0) return;
    if (Geometry->SectorsPerTrack == 0) return;

    Temp1 = Geometry->Heads * Geometry->SectorsPerTrack;
    Block->Cylinder = Sector / Temp1;
    Temp2 = Sector % Temp1;
    Block->Head = Temp2 / Geometry->SectorsPerTrack;
    Block->Sector = Temp2 % Geometry->SectorsPerTrack + 1;
}

/***************************************************************************/

BOOL WaitNotBusy(U32 Port, U32 TimeOut) {
    U32 Status;

    while (TimeOut) {
        Status = InPortByte(Port + HD_STATUS);
        if (Status & HD_STATUS_BUSY) continue;
        if (Status & HD_STATUS_READY) return TRUE;
        TimeOut--;
    }

    KernelLogText(LOG_WARNING, (LPSTR) "Time-out in HD");

    return FALSE;
}

/***************************************************************************/

static U32 HardDiskInitialize(void) {
    LPSTDHARDDISK Disk;
    LPATADRIVEID ATAID;
    U8 Buffer[SECTOR_SIZE];
    U32 Port;
    U32 Drive;
    U32 Index;

    DisableIRQ(HD_IRQ);

    //-------------------------------------
    // Identify the drives

    for (Port = 0; Port < 2; Port++) {
        U32 RealPort = 0;

        if (Port == 0) RealPort = HD_PORT_0;
        if (Port == 1) RealPort = HD_PORT_1;

        for (Drive = 0; Drive < 2; Drive++) {
            if (WaitNotBusy(RealPort, TIMEOUT) == FALSE) continue;

            OutPortByte(RealPort + HD_CYLINDERLOW, 0);
            OutPortByte(RealPort + HD_CYLINDERHIGH, 0);
            OutPortByte(RealPort + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4));
            OutPortByte(RealPort + HD_SECTOR, 0);
            OutPortByte(RealPort + HD_NUMSECTORS, 1);
            OutPortByte(RealPort + HD_COMMAND, HD_COMMAND_IDENTIFY);

            if (WaitNotBusy(RealPort, TIMEOUT) == FALSE) continue;

            InPortStringWord(RealPort, Buffer, SECTOR_SIZE / 2);

            ATAID = (LPATADRIVEID)Buffer;

            /*
            if
            (
              ATAID->PhysicalCylinders != 0 &&
              ATAID->PhysicalHeads     != 0 &&
              ATAID->PhysicalSectors   != 0
            )
            */
            {
                STR Message[64];
                STR PortStr[32];
                STR DriveStr[32];

                U32ToHexString(Port, PortStr);
                U32ToHexString(Drive, DriveStr);

                StringCopy(Message, TEXT("Initialize HD, Port : "));
                StringConcat(Message, PortStr);
                StringConcat(Message, TEXT(" Drive : "));
                StringConcat(Message, DriveStr);

                KernelLogText(LOG_VERBOSE, (LPSTR)Message);

                Disk = NewStdHardDisk();
                if (Disk == NULL) continue;

                Disk->Geometry.Cylinders = ATAID->PhysicalCylinders;
                Disk->Geometry.Heads = ATAID->PhysicalHeads;
                Disk->Geometry.SectorsPerTrack = ATAID->PhysicalSectors;
                Disk->Geometry.BytesPerSector = SECTOR_SIZE;
                Disk->IOPort = RealPort;
                Disk->IRQ = HD_IRQ;
                Disk->Drive = Drive;
                Disk->NumBuffers = NUM_BUFFERS;
                Disk->Buffer = KernelMemAlloc(NUM_BUFFERS * sizeof(SECTORBUFFER));

                //-------------------------------------
                // Clear the buffers

                for (Index = 0; Index < NUM_BUFFERS; Index++) {
                    Disk->Buffer[Index].SectorLow = MAX_U32;
                    Disk->Buffer[Index].SectorHigh = MAX_U32;
                    Disk->Buffer[Index].Score = 0;
                    Disk->Buffer[Index].Dirty = 0;
                }

                ListAddItem(Kernel.Disk, Disk);
            }
        }
    }

    EnableIRQ(HD_IRQ);

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
      KernelPrintString("HD : Controller still busy\n");
    }
    else
    if ((HD_Error = InPortByte(Port + HD_ERROR)) != 1)
    {
      KernelPrintString("HD : Controller reset failed\n");
    }
}
*/

/***************************************************************************/

void DriveOut(U32 Port, U32 Drive, U32 Command, U8* Buffer, U32 Cylinder, U32 Head, U32 Sector, U32 Count) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    if (WaitNotBusy(Port, TIMEOUT) == FALSE) goto Out;

    OutPortByte(Port + HD_CYLINDERLOW, Cylinder & 0xFF);
    OutPortByte(Port + HD_CYLINDERHIGH, (Cylinder >> 8) & 0xFF);
    OutPortByte(Port + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4) | (Head & 0x0F));
    OutPortByte(Port + HD_SECTOR, Sector & 0xFF);
    OutPortByte(Port + HD_NUMSECTORS, Count & 0xFF);
    OutPortByte(Port + HD_COMMAND, Command);

    if (WaitNotBusy(Port, TIMEOUT) == FALSE) goto Out;

    if (Command == HD_COMMAND_READ) {
        InPortStringWord(Port + HD_DATA, Buffer, (Count * SECTOR_SIZE) / 2);
    }

Out:

    RestoreFlags(&Flags);
}

/***************************************************************************/

U32 FindSectorInBuffers(LPSTDHARDDISK Disk, U32 SectorLow, U32 SectorHigh) {
    UNUSED(SectorHigh);

    U32 Index = 0;
    U32 BufNum = MAX_U32;

    for (Index = 0; Index < Disk->NumBuffers; Index++) {
        if (Disk->Buffer[Index].SectorLow == SectorLow) {
            Disk->Buffer[Index].Score++;
            BufNum = Index;
        } else {
            if (Disk->Buffer[Index].SectorLow != MAX_U32) {
                if (Disk->Buffer[Index].Score) Disk->Buffer[Index].Score--;
            }
        }
    }

    return BufNum;
}

/***************************************************************************/

static U32 GetEmptyBuffer(LPSTDHARDDISK Disk) {
    U32 Index;
    U32 Worst_Score = MAX_U32;
    U32 Worst_Index = MAX_U32;

    for (Index = 0; Index < Disk->NumBuffers; Index++) {
        if (Disk->Buffer[Index].SectorLow == MAX_U32) {
            return Index;
        } else if (Disk->Buffer[Index].SectorLow != MAX_U32) {
            if (Disk->Buffer[Index].Score < Worst_Score) {
                Worst_Score = Disk->Buffer[Index].Score;
                Worst_Index = Index;
            }
        }
    }

    //-------------------------------------
    // Invalidate the buffer and reset it's score

    if (Worst_Index != MAX_U32) {
        Disk->Buffer[Index].Score = 10;
        Disk->Buffer[Index].SectorLow = MAX_U32;
        Disk->Buffer[Index].SectorHigh = MAX_U32;
    }

    return Worst_Index;
}

/***************************************************************************/

static U32 Read(LPIOCONTROL Control) {
    LPSTDHARDDISK Disk;
    BLOCKPARAMS Params;
    U32 Index;
    U32 Current;

    //-------------------------------------
    // Check validity of parameters

    if (Control == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPSTDHARDDISK)Control->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    for (Current = 0; Current < Control->NumSectors; Current++) {
        //-------------------------------------
        // Let's see if we already have this sector

        Index = FindSectorInBuffers(Disk, Control->SectorLow + Current, 0);

        if (Index != MAX_U32) {
            MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), Disk->Buffer[Index].Data, SECTOR_SIZE);
        } else {
            //-------------------------------------
            // Get a new buffer in which to read the sector

            Index = GetEmptyBuffer(Disk);

            if (Index == MAX_U32) return DF_ERROR_UNEXPECT;

            //-------------------------------------
            // We must now do a physical disk access

            DisableIRQ(Disk->IRQ);

            SectorToBlockParams(&(Disk->Geometry), Control->SectorLow + Current, &Params);

            DriveOut(
                Disk->IOPort, Disk->Drive, HD_COMMAND_READ, Disk->Buffer[Index].Data, Params.Cylinder, Params.Head,
                Params.Sector, 1);

            EnableIRQ(Disk->IRQ);

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
    LPSTDHARDDISK Disk;

    // For security
    return DF_ERROR_NOTIMPL;

    if (Control == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPSTDHARDDISK)Control->Disk;
    if (Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
    if (Disk->IOPort == 0) return DF_ERROR_BADPARAM;
    if (Disk->IRQ == 0) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Check access permissions

    if (Disk->Access & DISK_ACCESS_READONLY) return DF_ERROR_NOPERM;

    return DF_ERROR_NOTIMPL;
}

/***************************************************************************/

static U32 GetInfo(LPDISKINFO Info) {
    LPSTDHARDDISK Disk;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPSTDHARDDISK)Info->Disk;
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
    LPSTDHARDDISK Disk;

    if (Access == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPSTDHARDDISK)Access->Disk;
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

    if (Busy) return;
    Busy = 1;

    // TODO : Add handling code

    Busy = 0;
}

/***************************************************************************/

U32 StdHardDiskCommands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return HardDiskInitialize();
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

/***************************************************************************/
