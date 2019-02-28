
// RAMDisk.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Kernel.h"

// Temporary

#include "FAT.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 RAMDiskCommands (U32, U32);

DRIVER RAMDiskDriver =
{
  ID_DRIVER,
  1,
  NULL,
  NULL,
  DRIVER_TYPE_RAMDISK,
  VER_MAJOR, VER_MINOR,
  "Exelsius",
  "IBM PC and compatibles",
  "RAM Disk Controller",
  RAMDiskCommands
};

/***************************************************************************/
// RAM physical disk, derives from PHYSICALDISK

typedef struct tag_RAMDISK
{
  PHYSICALDISK   Header;
  LINEAR         Base;
  U32            Size;
  U32            Access;   // Access parameters
} RAMDISK, *LPRAMDISK;

/***************************************************************************/

static LPRAMDISK NewRAMDisk ()
{
  LPRAMDISK This;

  This = (LPRAMDISK) KernelMemAlloc(sizeof(RAMDISK));

  if (This == NULL) return NULL;

  MemorySet(This, 0, sizeof(RAMDISK));

  This->Header.ID         = ID_DISK;
  This->Header.References = 1;
  This->Header.Next       = NULL;
  This->Header.Prev       = NULL;
  This->Header.Driver     = &RAMDiskDriver;
  This->Base              = NULL;
  This->Size              = 0;
  This->Access            = 0;

  return This;
}

/***************************************************************************/

static U32 CreateFATDirEntry
(
  LINEAR Buffer,
  LPSTR  Name,
  U32    Attributes,
  U32    Cluster
)
{
  LPFATDIRENTRY_EXT DirEntry   = NULL;
  LPFATDIRENTRY_LFN LFNEntry   = NULL;
  STR               ShortName [16];
  U32               NameIndex  = 0;
  U32               Checksum   = 0;
  U32               Ordinal    = 0;
  U32               Index      = 0;
  U32               Length     = 0;
  U32               NumEntries = 0;

  Length = StringLength(Name);

  //-------------------------------------
  // Create the short name

  for (Index = 0; Index < 6; Index++)
  {
    if (Name[Index] == '\0') break;
    ShortName[Index] = Name[Index];
  }

  ShortName[Index++] = '~';
  ShortName[Index++] = '1';

  for (; Index < 11; Index++)
  {
    ShortName[Index] = STR_SPACE;
  }

  //-------------------------------------
  // Compute checksum

  for (Index = 0; Index < 11; Index++)
  {
    Checksum =
    (
      ((Checksum & 0x01) << 7) |
      ((Checksum & 0xFE) >> 1)
    ) + ShortName[Index];
  }

  Checksum &= 0xFF;

  NumEntries = ((Length + 1) / 13) + 1;
  NumEntries++;

  //-------------------------------------
  // Fill the directory entry

  DirEntry = (LPFATDIRENTRY_EXT)
  (Buffer + ((NumEntries - 1) * sizeof(FATDIRENTRY_EXT)));

  DirEntry->Name[0]        = ShortName[0];
  DirEntry->Name[1]        = ShortName[1];
  DirEntry->Name[2]        = ShortName[2];
  DirEntry->Name[3]        = ShortName[3];
  DirEntry->Name[4]        = ShortName[4];
  DirEntry->Name[5]        = ShortName[5];
  DirEntry->Name[6]        = ShortName[6];
  DirEntry->Name[7]        = ShortName[7];

  DirEntry->Ext[0]         = ShortName[8];
  DirEntry->Ext[1]         = ShortName[9];
  DirEntry->Ext[2]         = ShortName[10];

  DirEntry->Attributes     = Attributes;
  DirEntry->NT             = 0;
  DirEntry->CreationMS     = 0;
  DirEntry->CreationHM     = 0;
  DirEntry->CreationYM     = 0;
  DirEntry->LastAccessDate = 0;
  DirEntry->ClusterHigh    = (Cluster & 0xFFFF0000) >> 16;
  DirEntry->Time           = 0;
  DirEntry->Date           = 0;
  DirEntry->ClusterLow     = (Cluster & 0xFFFF);
  DirEntry->Size           = 0;

  //-------------------------------------
  // Store the long name

  LFNEntry = (LPFATDIRENTRY_LFN) DirEntry;

  Index   = 0;
  Ordinal = 1;

  while (1)
  {
    LFNEntry--;

    LFNEntry->Ordinal    = Ordinal++;
    LFNEntry->Checksum   = Checksum;
    LFNEntry->Attributes = FAT_ATTR_VOLUME;

    if (Name[Index]) LFNEntry->Char01 = (USTR) Name[Index++];
    else { LFNEntry->Char01 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char02 = (USTR) Name[Index++];
    else { LFNEntry->Char02 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char03 = (USTR) Name[Index++];
    else { LFNEntry->Char03 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char04 = (USTR) Name[Index++];
    else { LFNEntry->Char04 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char05 = (USTR) Name[Index++];
    else { LFNEntry->Char05 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char06 = (USTR) Name[Index++];
    else { LFNEntry->Char06 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char07 = (USTR) Name[Index++];
    else { LFNEntry->Char07 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char08 = (USTR) Name[Index++];
    else { LFNEntry->Char08 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char09 = (USTR) Name[Index++];
    else { LFNEntry->Char09 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char10 = (USTR) Name[Index++];
    else { LFNEntry->Char10 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char11 = (USTR) Name[Index++];
    else { LFNEntry->Char11 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char12 = (USTR) Name[Index++];
    else { LFNEntry->Char12 = (USTR) '\0'; break; }
    if (Name[Index]) LFNEntry->Char13 = (USTR) Name[Index++];
    else { LFNEntry->Char13 = (USTR) '\0'; break; }
  }

  LFNEntry->Ordinal |= BIT_6;

  return NumEntries * sizeof(FATDIRENTRY_EXT);
}

/***************************************************************************/

static BOOL FormatRAMDisk_FAT32 (LINEAR Base, U32 Size)
{
  LPFAT32MBR Master         = NULL;

  U32*       FAT            = NULL;
  U32        FATStart1      = 0;
  U32        FATStart2      = 0;
  U32        DataStart      = 0;
  U32        CurrentCluster = 0;
  U32        CurrentSector  = 0;
  U32        CurrentBase    = 0;
  U32        DirEntrySize   = 0;
  U32        ClusterEntry1  = 0;
  U32        ClusterEntry2  = 0;
  U32        ClusterEntry3  = 0;
  U32        ClusterEntry4  = 0;
  U32        ClusterEntry5  = 0;
  U32        ClusterEntry6  = 0;

  //-------------------------------------
  // Create the Master Boot Record

  Master = (LPFAT32MBR) Base;

  Master->OEMName[0]         = 'M';
  Master->OEMName[1]         = 'S';
  Master->OEMName[2]         = 'W';
  Master->OEMName[3]         = 'I';
  Master->OEMName[4]         = 'N';
  Master->OEMName[5]         = '4';
  Master->OEMName[6]         = '.';
  Master->OEMName[7]         = '1';
  Master->BytesPerSector     = 512;
  Master->SectorsPerCluster  = 8;
  Master->ReservedSectors    = 3;
  Master->NumFATs            = 2;
  Master->NumRootEntries_NA  = 0;
  Master->NumSectors_NA      = 0;
  Master->MediaDescriptor    = 0xF8;
  Master->SectorsPerFAT_NA   = 0;
  Master->SectorsPerTrack    = 63;
  Master->NumHeads           = 255;
  Master->NumHiddenSectors   = 127;
  Master->NumSectors         = Size / SECTOR_SIZE;
  Master->NumSectorsPerFAT   = 4;
  Master->Flags              = 0;
  Master->Version            = 0;
  Master->RootCluster        = 2;
  Master->InfoSector         = 1;
  Master->BackupBootSector   = 6;
  Master->LogicalDriveNumber = 0x80;
  Master->Reserved2          = 0;
  Master->ExtendedSignature  = 0x29;
  Master->SerialNumber       = 0x63482951;
  Master->FATName[0]         = 'F';
  Master->FATName[1]         = 'A';
  Master->FATName[2]         = 'T';
  Master->FATName[3]         = '3';
  Master->FATName[4]         = '2';
  Master->FATName[5]         = ' ';
  Master->FATName[6]         = ' ';
  Master->FATName[7]         = ' ';
  Master->BIOSMark          = 0xAA55;

  FATStart1 = Master->ReservedSectors;
  FATStart2 = FATStart1 + Master->NumSectorsPerFAT;
  DataStart = FATStart1 + (Master->NumFATs * Master->NumSectorsPerFAT);

  //-------------------------------------
  // Create the root

  CurrentCluster = Master->RootCluster;
  CurrentSector  = DataStart + ((CurrentCluster - Master->RootCluster) * Master->SectorsPerCluster);
  CurrentBase    = Base + (CurrentSector * SECTOR_SIZE);
  ClusterEntry1  = Master->RootCluster + 1;
  ClusterEntry2  = Master->RootCluster + 2;
  ClusterEntry3  = Master->RootCluster + 3;
  ClusterEntry4  = Master->RootCluster + 4;
  ClusterEntry5  = Master->RootCluster + 5;
  ClusterEntry6  = Master->RootCluster + 6;

  FAT = (U32*) (Base + (FATStart1 * SECTOR_SIZE));

  FAT[0] = FAT32_CLUSTER_LAST;
  FAT[1] = FAT32_CLUSTER_LAST;
  FAT[2] = FAT32_CLUSTER_LAST;
  FAT[3] = FAT32_CLUSTER_LAST;
  FAT[4] = FAT32_CLUSTER_LAST;
  FAT[5] = FAT32_CLUSTER_LAST;

  //-------------------------------------
  // Create some directories

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "EXOS",
    FAT_ATTR_FOLDER,
    ClusterEntry1
  );

  CurrentBase += DirEntrySize;

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "Program Files",
    FAT_ATTR_FOLDER,
    ClusterEntry2
  );

  CurrentBase += DirEntrySize;

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "Boot.log",
    FAT_ATTR_ARCHIVE,
    ClusterEntry3
  );

  //-------------------------------------
  // Create some subdirectories

  CurrentCluster = ClusterEntry1;
  CurrentSector  = DataStart + ((CurrentCluster - Master->RootCluster) * Master->SectorsPerCluster);
  CurrentBase    = Base + (CurrentSector * SECTOR_SIZE);

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "Users",
    FAT_ATTR_FOLDER,
    ClusterEntry4
  );

  CurrentBase += DirEntrySize;

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "Libraries",
    FAT_ATTR_FOLDER,
    ClusterEntry5
  );

  CurrentBase += DirEntrySize;

  DirEntrySize = CreateFATDirEntry
  (
    CurrentBase,
    "Temp",
    FAT_ATTR_FOLDER,
    ClusterEntry6
  );

  return TRUE;
}

/***************************************************************************/

static U32 RAMDiskInitialize ()
{
  PARTITION_CREATION Create;
  LPBOOTPARTITION    Partition;
  LPRAMDISK          Disk;

  Disk = NewRAMDisk();
  if (Disk == NULL) return DF_ERROR_NOMEMORY;

  Disk->Size = N_512KB;
  Disk->Base = VirtualAlloc(LA_RAMDISK, Disk->Size, ALLOC_PAGES_COMMIT);

  if (Disk->Base == NULL)
  {
    VirtualFree(LA_RAMDISK, Disk->Size);
    return DF_ERROR_NOMEMORY;
  }

  //-------------------------------------
  // Purge the disk

  MemorySet((LPVOID) Disk->Base, 0, Disk->Size);

/*
  //-------------------------------------
  // Initialize the partitions

  Partition = (LPBOOTPARTITION) (Disk->Base + MBR_PARTITION_START);

  Partition->Disk              = 0x80;
  Partition->StartCHS.Head     = 0;
  Partition->StartCHS.Cylinder = 0;
  Partition->StartCHS.Sector   = 0;
  Partition->Type              = FSID_DOS_FAT32;
  Partition->EndCHS.Head       = 0;
  Partition->EndCHS.Cylinder   = 0;
  Partition->EndCHS.Sector     = 0;
  Partition->LBA               = 2;
  Partition->Size              = (Disk->Size - (Partition->LBA * SECTOR_SIZE)) / SECTOR_SIZE;

  //-------------------------------------
  // Format and register the disk

  FormatRAMDisk_FAT32
  (
    Disk->Base + (Partition->LBA * SECTOR_SIZE),
    Disk->Size - (Partition->LBA * SECTOR_SIZE)
  );
*/

  //-------------------------------------
  // Initialize the partitions

  Partition = (LPBOOTPARTITION) (Disk->Base + MBR_PARTITION_START);

  Partition->Disk              = 0x80;
  Partition->StartCHS.Head     = 0;
  Partition->StartCHS.Cylinder = 0;
  Partition->StartCHS.Sector   = 0;
  Partition->Type              = FSID_EXOS;
  Partition->EndCHS.Head       = 0;
  Partition->EndCHS.Cylinder   = 0;
  Partition->EndCHS.Sector     = 0;
  Partition->LBA               = 2;
  Partition->Size              = (Disk->Size - (Partition->LBA * SECTOR_SIZE)) / SECTOR_SIZE;

  //-------------------------------------
  // Create an XFS partition

  Create.Size                 = sizeof(PARTITION_CREATION);
  Create.Disk                 = (LPPHYSICALDISK) Disk;
  Create.PartitionStartSector = 2;
  Create.PartitionNumSectors  = Partition->Size;
  Create.SectorsPerCluster    = 8;
  Create.Flags                = 0;

  StringCopy(Create.VolumeName, "RamDisk");

  XFSDriver.Command(DF_FS_CREATEPARTITION, (U32) &Create);

  //-------------------------------------

  ListAddItem(Kernel.Disk, Disk);

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 Read (LPIOCONTROL Control)
{
  LPRAMDISK Disk;

  //-------------------------------------
  // Get the physical disk to which operation applies

  Disk = (LPRAMDISK) Control->Disk;
  if (Disk == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check validity of parameters

  if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
  if (Disk->Base == NULL) return DF_ERROR_BADPARAM;
  if (Disk->Size == 0) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check if we are in the limits of the disk

  if ((Control->SectorLow * SECTOR_SIZE) >= Disk->Size)
  {
    return DF_ERROR_GENERIC;
  }

  //-------------------------------------
  // Copy the sectors to the user's buffer

  MemoryCopy
  (
    Control->Buffer,
    (LPVOID) (Disk->Base + (Control->SectorLow * SECTOR_SIZE)),
    Control->NumSectors * SECTOR_SIZE
  );

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 Write (LPIOCONTROL Control)
{
  LPRAMDISK Disk;

  if (Control == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the physical disk to which operation applies

  Disk = (LPRAMDISK) Control->Disk;
  if (Disk == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check validity of parameters

  if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
  if (Disk->Base == NULL) return DF_ERROR_BADPARAM;
  if (Disk->Size == 0) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check access permissions

  if (Disk->Access & DISK_ACCESS_READONLY) return DF_ERROR_NOPERM;

  //-------------------------------------
  // Check if we are in the limits of the disk

  if
  (
    (
      (Control->SectorLow * SECTOR_SIZE) +
      (Control->NumSectors * SECTOR_SIZE)
    ) >= Disk->Size
  )
  {
    return DF_ERROR_BADPARAM;
  }

  //-------------------------------------
  // Copy the user's buffer to the disk

  MemoryCopy
  (
    (LPVOID) (Disk->Base + (Control->SectorLow * SECTOR_SIZE)),
    Control->Buffer,
    Control->NumSectors * SECTOR_SIZE
  );

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 GetInfo (LPDISKINFO Info)
{
  LPRAMDISK Disk;

  if (Info == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the physical disk to which operation applies

  Disk = (LPRAMDISK) Info->Disk;
  if (Disk == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check validity of parameters

  if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
  if (Disk->Base == NULL) return DF_ERROR_BADPARAM;
  if (Disk->Size == 0) return DF_ERROR_BADPARAM;

  //-------------------------------------

  Info->Type       = DRIVER_TYPE_RAMDISK;
  Info->Removable  = 0;
  Info->NumSectors = Disk->Size / SECTOR_SIZE;
  Info->Access     = Disk->Access;

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 SetAccess (LPDISKACCESS Access)
{
  LPRAMDISK Disk;

  if (Access == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the physical disk to which operation applies

  Disk = (LPRAMDISK) Access->Disk;
  if (Disk == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Check validity of parameters

  if (Disk->Header.ID != ID_DISK) return DF_ERROR_BADPARAM;
  if (Disk->Base == NULL) return DF_ERROR_BADPARAM;
  if (Disk->Size == 0) return DF_ERROR_BADPARAM;

  //-------------------------------------

  Disk->Access = Access->Access;

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

U32 RAMDiskCommands (U32 Function, U32 Parameter)
{
  switch (Function)
  {
    case DF_LOAD           : return RAMDiskInitialize();
    case DF_UNLOAD         : return DF_ERROR_SUCCESS;
    case DF_GETVERSION     : return MAKE_VERSION(VER_MAJOR, VER_MINOR);
    case DF_DISK_RESET     : return DF_ERROR_NOTIMPL;
    case DF_DISK_READ      : return Read((LPIOCONTROL) Parameter);
    case DF_DISK_WRITE     : return Write((LPIOCONTROL) Parameter);
    case DF_DISK_GETINFO   : return GetInfo((LPDISKINFO) Parameter);
    case DF_DISK_SETACCESS : return SetAccess((LPDISKACCESS) Parameter);
  }

  return DF_ERROR_NOTIMPL;
}

/***************************************************************************/
