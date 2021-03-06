
// FAT16.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Kernel.h"
#include "FileSys.h"
#include "FAT.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 FAT16Commands (U32, U32);

DRIVER FAT16Driver =
{
  ID_DRIVER, 1,
  NULL, NULL,
  DRIVER_TYPE_FILESYSTEM,
  VER_MAJOR, VER_MINOR,
  "Exelsius",
  "Microsoft Corporation",
  "Fat 16 File System",
  FAT16Commands
};

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_FAT16FILESYSTEM
{
  FILESYSTEM     Header;
  LPPHYSICALDISK Disk;
  FAT16MBR       Master;
  SECTOR         PartitionStart;
  U32            PartitionSize;
  SECTOR         FATStart;
  SECTOR         FATStart2;
  SECTOR         DataStart;
  U32            SectorsInRoot;
  U32            BytesPerCluster;
  U8*            IOBuffer;
} FAT16FILESYSTEM, *LPFAT16FILESYSTEM;

/***************************************************************************/

typedef struct tag_FATFILE
{
  FILE Header;
  FATFILELOC Location;
} FATFILE, *LPFATFILE;

/***************************************************************************/

static LPFAT16FILESYSTEM NewFAT16FileSystem (LPPHYSICALDISK Disk)
{
  LPFAT16FILESYSTEM This;

  This = (LPFAT16FILESYSTEM) KernelMemAlloc(sizeof(FAT16FILESYSTEM));
  if (This == NULL) return NULL;

  MemorySet(This, 0, sizeof(FAT16FILESYSTEM));

  This->Header.ID         = ID_FILESYSTEM;
  This->Header.References = 1;
  This->Header.Next       = NULL;
  This->Header.Prev       = NULL;
  This->Header.Driver     = &FAT16Driver;
  This->Disk              = Disk;
  This->FATStart          = 0;
  This->FATStart2         = 0;
  This->DataStart         = 0;
  This->BytesPerCluster   = 0;
  This->IOBuffer          = NULL;

  InitSemaphore(&(This->Header.Semaphore));

  //-------------------------------------
  // Assign a default name to the file system

  GetDefaultFileSystemName(This->Header.Name);

  return This;
}

/***************************************************************************/

static LPFATFILE NewFATFile
(
  LPFAT16FILESYSTEM FileSystem,
  LPFATFILELOC FileLoc
)
{
  LPFATFILE This;

  This = (LPFATFILE) KernelMemAlloc(sizeof(FATFILE));
  if (This == NULL) return NULL;

  MemorySet(This, 0, sizeof(FATFILE));

  This->Header.ID                   = ID_FILE;
  This->Header.References           = 1;
  This->Header.Next                 = NULL;
  This->Header.Prev                 = NULL;
  This->Header.FileSystem           = (LPFILESYSTEM) FileSystem;
  This->Location.PreviousCluster    = FileLoc->PreviousCluster;
  This->Location.FolderCluster      = FileLoc->FolderCluster;
  This->Location.FileCluster        = FileLoc->FileCluster;
  This->Location.DataCluster        = FileLoc->DataCluster;
  This->Location.Offset             = FileLoc->Offset;

  InitSemaphore(&(This->Header.Semaphore));
  InitSecurity(&(This->Header.Security));

  return This;
}

/***************************************************************************/

BOOL MountPartition_FAT16
(
  LPPHYSICALDISK Disk,
  LPBOOTPARTITION Partition,
  U32 Base
)
{
  U8                Buffer [SECTOR_SIZE];
  IOCONTROL         Control;
  LPFAT16MBR        Master;
  LPFAT16FILESYSTEM FileSystem;
  U32               Result;

  Control.ID         = ID_IOCONTROL;
  Control.Disk       = Disk;
  Control.SectorLow  = Base + Partition->LBA;
  Control.SectorHigh = 0;
  Control.NumSectors = 1;
  Control.Buffer     = (LPVOID) Buffer;
  Control.BufferSize = SECTOR_SIZE;

  Result = Disk->Driver->Command(DF_DISK_READ, (U32) &Control);

  if (Result != DF_ERROR_SUCCESS) return FALSE;

  //-------------------------------------
  // Assign a pointer to the sector

  Master = (LPFAT16MBR) Buffer;

  //-------------------------------------
  // Check if this is really a FAT16 partition

  if (Master->FATName[0] != 'F') return FALSE;
  if (Master->FATName[1] != 'A') return FALSE;
  if (Master->FATName[2] != 'T') return FALSE;
  if (Master->FATName[3] != '1') return FALSE;
  if (Master->FATName[4] != '6') return FALSE;

  //-------------------------------------
  // Check for presence of BIOS mark

  if (Master->BIOSMark != 0xAA55) return FALSE;

  //-------------------------------------
  // Create the file system object

  FileSystem = NewFAT16FileSystem(Disk);
  if (FileSystem == NULL) return FALSE;

  //-------------------------------------
  // Copy the Master Sector

  MemoryCopy(&(FileSystem->Master), Buffer, SECTOR_SIZE);

  FileSystem->PartitionStart  = Base + Partition->LBA;
  FileSystem->PartitionSize   = Partition->Size;
  FileSystem->BytesPerCluster = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

  FileSystem->IOBuffer = (U8*) KernelMemAlloc
  (
    FileSystem->Master.SectorsPerCluster * SECTOR_SIZE
  );

  //-------------------------------------
  // Compute the start of the FAT

  FileSystem->FATStart =
    FileSystem->PartitionStart +
    FileSystem->Master.ReservedSectors;

  if (FileSystem->Master.NumFATs > 1)
  {
    FileSystem->FATStart2 = FileSystem->FATStart +
    FileSystem->Master.SectorsPerFAT;
  }

  //-------------------------------------
  // Compute the start of the data

  FileSystem->DataStart = FileSystem->FATStart +
  (FileSystem->Master.NumFATs * FileSystem->Master.SectorsPerFAT);

  FileSystem->SectorsInRoot =
  (
    FileSystem->Master.NumRootEntries * sizeof(FATDIRENTRY)
  ) / (U32) FileSystem->Master.BytesPerSector;

  //-------------------------------------
  // Register the file system

  ListAddItem(Kernel.FileSystem, FileSystem);

  return TRUE;
}

/***************************************************************************/

static BOOL ReadCluster
(
  LPFAT16FILESYSTEM FileSystem,
  CLUSTER Cluster,
  LPVOID Buffer
)
{
  IOCONTROL Control;
  SECTOR Sector;
  U32 NumSectors;
  U32 Result;

  // Cluster 1 does not exist in FAT16 but here it
  // is assumed to be the root directory

  if (Cluster == 1)
  {
    Sector = FileSystem->DataStart;
    NumSectors = FileSystem->SectorsInRoot;
    if (NumSectors > FileSystem->Master.SectorsPerCluster)
    {
      NumSectors = FileSystem->Master.SectorsPerCluster;
    }
  }
  else
  {
    Sector = FileSystem->DataStart +
    FileSystem->SectorsInRoot +
    (
      (Cluster - 2) * FileSystem->Master.SectorsPerCluster
    );
    NumSectors = FileSystem->Master.SectorsPerCluster;
  }

  if
  (
    Sector < FileSystem->PartitionStart ||
    Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize
  )
  {
    return FALSE;
  }

  Control.ID         = ID_IOCONTROL;
  Control.Disk       = FileSystem->Disk;
  Control.SectorLow  = Sector;
  Control.SectorHigh = 0;
  Control.NumSectors = NumSectors;
  Control.Buffer     = Buffer;
  Control.BufferSize = NumSectors * SECTOR_SIZE;

  Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32) &Control);

  if (Result != DF_ERROR_SUCCESS) return FALSE;

  return TRUE;
}

/***************************************************************************/

static BOOL WriteCluster
(
  LPFAT16FILESYSTEM FileSystem,
  CLUSTER Cluster,
  LPVOID Buffer
)
{
  IOCONTROL Control;
  SECTOR    Sector;
  U32       NumSectors;
  U32       Result;

  // Cluster 1 does not exist in FAT16 but here it
  // is assumed to be the root directory

  if (Cluster == 1)
  {
    Sector = FileSystem->DataStart;
    NumSectors = FileSystem->SectorsInRoot;
    if (NumSectors > FileSystem->Master.SectorsPerCluster)
    {
      NumSectors = FileSystem->Master.SectorsPerCluster;
    }
  }
  else
  {
    Sector = FileSystem->DataStart +
    FileSystem->SectorsInRoot +
    (
      (Cluster - 2) * FileSystem->Master.SectorsPerCluster
    );
    NumSectors = FileSystem->Master.SectorsPerCluster;
  }

  if
  (
    Sector < FileSystem->PartitionStart ||
    Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize
  )
  {
    return FALSE;
  }

  Control.ID         = ID_IOCONTROL;
  Control.Disk       = FileSystem->Disk;
  Control.SectorLow  = Sector;
  Control.SectorHigh = 0;
  Control.NumSectors = NumSectors;
  Control.Buffer     = Buffer;
  Control.BufferSize = NumSectors * SECTOR_SIZE;

  Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32) &Control);

  if (Result != DF_ERROR_SUCCESS) return FALSE;

  return TRUE;
}

/***************************************************************************/

static CLUSTER GetNextClusterInChain
(
  LPFAT16FILESYSTEM FileSystem,
  CLUSTER Cluster
)
{
  U16       Buffer [SECTOR_SIZE / sizeof(U16)];
  IOCONTROL Control;
  CLUSTER   NextCluster;
  U32       NumEntriesPerSector;
  U32       Sector;
  U32       Offset;
  U32       Result;

  NextCluster = FAT16_CLUSTER_LAST;

  NumEntriesPerSector = SECTOR_SIZE / sizeof(U16);
  Sector = Cluster / NumEntriesPerSector;
  Offset = Cluster % NumEntriesPerSector;

  Control.ID         = ID_IOCONTROL;
  Control.Disk       = FileSystem->Disk;
  Control.SectorLow  = FileSystem->FATStart + Sector;
  Control.SectorHigh = 0;
  Control.NumSectors = 1;
  Control.Buffer     = Buffer;
  Control.BufferSize = SECTOR_SIZE;

  Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32) &Control);

  if (Result == DF_ERROR_SUCCESS)
  {
    NextCluster = Buffer[Offset];
  }

  return NextCluster;
}

/***************************************************************************/

static void DecodeFileName (LPFATDIRENTRY DirEntry, LPSTR Name)
{
  LPFATDIRENTRY_LFN LFNEntry = NULL;
  U32               Index = 0;

  //-------------------------------------
  // 8.3 names

  for (Index = 0; Index < 8; Index++)
  {
    if (DirEntry->Name[Index] == STR_SPACE) break;
    *Name++ = DirEntry->Name[Index];
  }

  if (DirEntry->Ext[0] != STR_SPACE)
  {
    *Name++ = STR_DOT;
    for (Index = 0; Index < 3; Index++)
    {
      if (DirEntry->Ext[Index] == STR_SPACE) break;
      *Name++ = DirEntry->Ext[Index];
    }
  }

  *Name++ = STR_NULL;
}

/***************************************************************************/

static void TranslateFileInfo
(
  LPFATDIRENTRY DirEntry,
  LPFATFILE File
)
{
  //-------------------------------------
  // Translate the attributes

  File->Header.Attributes = 0;

  if (DirEntry->Attributes & FAT_ATTR_FOLDER)
  {
    File->Header.Attributes |= FS_ATTR_FOLDER;
  }

  if (DirEntry->Attributes & FAT_ATTR_READONLY)
  {
    File->Header.Attributes |= FS_ATTR_READONLY;
  }

  if (DirEntry->Attributes & FAT_ATTR_HIDDEN)
  {
    File->Header.Attributes |= FS_ATTR_HIDDEN;
  }

  if (DirEntry->Attributes & FAT_ATTR_SYSTEM)
  {
    File->Header.Attributes |= FS_ATTR_SYSTEM;
  }

  //-------------------------------------
  // Translate the size

  File->Header.SizeLow  = DirEntry->Size;
  File->Header.SizeHigh = 0;

  //-------------------------------------
  // Translate the time

  File->Header.Modified.Year   = ((DirEntry->Date & FAT_DATE_YEAR_MASK) >> FAT_DATE_YEAR_SHFT) + 1980;
  File->Header.Modified.Month  = (DirEntry->Date & FAT_DATE_MONTH_MASK) >> FAT_DATE_MONTH_SHFT;
  File->Header.Modified.Day    = (DirEntry->Date & FAT_DATE_DAY_MASK) >> FAT_DATE_DAY_SHFT;
  File->Header.Modified.Hour   = (DirEntry->Time & FAT_TIME_HOUR_MASK) >> FAT_TIME_HOUR_SHFT;
  File->Header.Modified.Minute = (DirEntry->Time & FAT_TIME_MINUTE_MASK) >> FAT_TIME_MINUTE_SHFT;
  File->Header.Modified.Second = ((DirEntry->Time & FAT_TIME_SECOND_MASK) >> FAT_TIME_SECOND_SHFT) * 2;
  File->Header.Modified.Milli  = 0;
}

/***************************************************************************/

static BOOL LocateFile
(
  LPFAT16FILESYSTEM FileSystem,
  LPCSTR Path,
  LPFATFILELOC FileLoc
)
{
  STR           Component [MAX_FILE_NAME];
  STR           Name [MAX_FILE_NAME];
  LPFATDIRENTRY DirEntry;
  U32           PathIndex = 0;
  U32           CompIndex = 0;

  FileLoc->PreviousCluster = 0;
  FileLoc->FolderCluster   = 1;
  FileLoc->FileCluster     = FileLoc->FolderCluster;
  FileLoc->Offset          = 0;
  FileLoc->DataCluster     = 0;

  //-------------------------------------
  // Read the root cluster

  if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer))
  {
    return FALSE;
  }

  while (1)
  {
    //-------------------------------------
    // Parse the next component to look for

NextComponent :

    CompIndex = 0;

    while (1)
    {
      if (Path[PathIndex] == STR_SLASH)
      {
        Component[CompIndex] = STR_NULL;
        PathIndex++;
        break;
      }
      else
      if (Path[PathIndex] == STR_NULL)
      {
        Component[CompIndex] = STR_NULL;
        break;
      }
      else
      {
        Component[CompIndex++] = Path[PathIndex++];
      }
    }

    //-------------------------------------
    // Loop through all directory entries

    while (1)
    {
      DirEntry = (LPFATDIRENTRY) (FileSystem->IOBuffer + FileLoc->Offset);

      if
      (
        (DirEntry->Cluster) &&
        (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
        (DirEntry->Name[0] != 0xE5)
      )
      {
        DecodeFileName(DirEntry, Name);

        if
        (
          StringCompareNC(Component, "*") == 0 ||
          StringCompareNC(Component, Name) == 0
        )
        {
          if (Path[PathIndex] == STR_NULL)
          {
            FileLoc->DataCluster = DirEntry->Cluster;

            return TRUE;
          }
          else
          {
            if (DirEntry->Attributes & FAT_ATTR_FOLDER)
            {
              FileLoc->FolderCluster = DirEntry->Cluster;
              FileLoc->FileCluster   = FileLoc->FolderCluster;
              FileLoc->Offset        = 0;

              if
              (
                ReadCluster
                (
                  FileSystem,
                  FileLoc->FileCluster,
                  FileSystem->IOBuffer
                ) == FALSE
              ) return FALSE;

              goto NextComponent;
            }
            else
            {
              return FALSE;
            }
          }
        }
      }

      //-------------------------------------
      // Advance to the next entry

      FileLoc->Offset += sizeof(FATDIRENTRY);

      if (FileLoc->Offset >= FileSystem->BytesPerCluster)
      {
        FileLoc->Offset = 0;
        FileLoc->FileCluster = GetNextClusterInChain
        (
          FileSystem,
          FileLoc->FileCluster
        );

        if
        (
          FileLoc->FileCluster == 0 ||
          FileLoc->FileCluster >= FAT16_CLUSTER_RESERVED
        ) return FALSE;

        if
        (
          ReadCluster
          (
            FileSystem,
            FileLoc->FileCluster,
            FileSystem->IOBuffer
          ) == FALSE
        ) return FALSE;
      }
    }
  }
}

/***************************************************************************/

static U32 Initialize ()
{
  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static LPFATFILE OpenFile (LPFILEINFO Find)
{
  LPFAT16FILESYSTEM FileSystem = NULL;
  LPFATFILE         File       = NULL;
  LPFATDIRENTRY     DirEntry   = NULL;
  FATFILELOC        FileLoc;

  //-------------------------------------
  // Check validity of parameters

  if (Find == NULL) return NULL;

  //-------------------------------------
  // Get the associated file system

  FileSystem = (LPFAT16FILESYSTEM) Find->FileSystem;

  if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE)
  {
    if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer)
    == FALSE) return FALSE;

    DirEntry = (LPFATDIRENTRY) (FileSystem->IOBuffer + FileLoc.Offset);

    File = NewFATFile(FileSystem, &FileLoc);
    if (File == NULL) return NULL;

    DecodeFileName(DirEntry, File->Header.Name);
    TranslateFileInfo(DirEntry, File);
  }

  return File;
}

/***************************************************************************/

static U32 OpenNext (LPFATFILE File)
{
  LPFAT16FILESYSTEM FileSystem = NULL;
  LPFATDIRENTRY     DirEntry   = NULL;

  //-------------------------------------
  // Check validity of parameters

  if (File == NULL) return DF_ERROR_BADPARAM;
  if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the associated file system

  FileSystem = (LPFAT16FILESYSTEM) File->Header.FileSystem;

  //-------------------------------------
  // Read the cluster containing the file

  if
  (
    ReadCluster
    (
      FileSystem,
      File->Location.FileCluster,
      FileSystem->IOBuffer
    ) == FALSE
  ) return DF_ERROR_IO;

  while (1)
  {
    File->Location.Offset += sizeof(FATDIRENTRY);

    if (File->Location.Offset >= FileSystem->BytesPerCluster)
    {
      File->Location.Offset = 0;

      File->Location.FileCluster =
      GetNextClusterInChain(FileSystem, File->Location.FileCluster);

      if
      (
        File->Location.FileCluster == 0 ||
        File->Location.FileCluster >= FAT16_CLUSTER_RESERVED
      ) return DF_ERROR_GENERIC;

      if
      (
        ReadCluster
        (
          FileSystem,
          File->Location.FileCluster,
          FileSystem->IOBuffer
        ) == FALSE
      ) return DF_ERROR_IO;
    }

    DirEntry = (LPFATDIRENTRY) (FileSystem->IOBuffer + File->Location.Offset);

    if
    (
      (DirEntry->Cluster) &&
      (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
      (DirEntry->Name[0] != 0xE5)
    )
    {
      File->Location.DataCluster = DirEntry->Cluster;
      DecodeFileName(DirEntry, File->Header.Name);
      TranslateFileInfo(DirEntry, File);
      break;
    }
  }

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CloseFile (LPFATFILE File)
{
  LPFAT16FILESYSTEM FileSystem;
  LPFATDIRENTRY     DirEntry;

  if (File == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the associated file system

  FileSystem = (LPFAT16FILESYSTEM) File->Header.FileSystem;

  //-------------------------------------
  // Update file information in directory entry

/*
  if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE)
  {
    return DF_ERROR_IO;
  }

  DirEntry = (LPFATDIRENTRY) (FileSystem->IOBuffer + File->Location.Offset);

  if (File->Header.SizeLow > DirEntry->Size)
  {
    DirEntry->Size = File->Header.SizeLow;

    if (WriteCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE)
    {
      return DF_ERROR_IO;
    }
  }
*/

  File->Header.ID = ID_NONE;

  KernelMemFree(File);

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 ReadFile (LPFATFILE File)
{
  LPFAT16FILESYSTEM FileSystem;
  CLUSTER           RelativeCluster;
  CLUSTER           Cluster;
  U32               OffsetInCluster;
  U32               BytesRemaining;
  U32               BytesToRead;
  U32               Index;

  //-------------------------------------
  // Check validity of parameters

  if (File == NULL) return DF_ERROR_BADPARAM;
  if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;
  if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the associated file system

  FileSystem = (LPFAT16FILESYSTEM) File->Header.FileSystem;

  //-------------------------------------
  // Compute the starting cluster and the offset

  RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
  OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
  BytesRemaining  = File->Header.BytesToRead;
  File->Header.BytesRead = 0;

  Cluster = File->Location.DataCluster;

  for (Index = 0; Index < RelativeCluster; Index++)
  {
    Cluster = GetNextClusterInChain(FileSystem, Cluster);
    if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED)
    {
      return DF_ERROR_IO;
    }
  }

  while (1)
  {
    //-------------------------------------
    // Read the current data cluster

    if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE)
    {
      return DF_ERROR_IO;
    }

    BytesToRead = FileSystem->BytesPerCluster - OffsetInCluster;
    if (BytesToRead > BytesRemaining) BytesToRead = BytesRemaining;

    //-------------------------------------
    // Copy the data to the user buffer

    MemoryCopy
    (
      ((U8*) File->Header.Buffer) + File->Header.BytesRead,
      FileSystem->IOBuffer + OffsetInCluster,
      BytesToRead
    );

    //-------------------------------------
    // Update counters

    OffsetInCluster         = 0;
    BytesRemaining         -= BytesToRead;
    File->Header.BytesRead += BytesToRead;
    File->Header.Position  += BytesToRead;

    //-------------------------------------
    // Check if we read all data

    if (BytesRemaining == 0) break;

    //-------------------------------------
    // Get the next cluster in the chain

    Cluster = GetNextClusterInChain(FileSystem, Cluster);

    if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED)
    {
      break;
    }
  }

  return DF_ERROR_SUCCESS;
}

/***************************************************************************/

U32 FAT16Commands (U32 Function, U32 Parameter)
{
  switch (Function)
  {
    case DF_LOAD             : return Initialize();
    case DF_GETVERSION       : return MAKE_VERSION(VER_MAJOR, VER_MINOR);
    case DF_FS_GETVOLUMEINFO : return DF_ERROR_NOTIMPL;
    case DF_FS_SETVOLUMEINFO : return DF_ERROR_NOTIMPL;
    case DF_FS_CREATEFOLDER  : return DF_ERROR_NOTIMPL;
    case DF_FS_DELETEFOLDER  : return DF_ERROR_NOTIMPL;
    case DF_FS_RENAMEFOLDER  : return DF_ERROR_NOTIMPL;
    case DF_FS_OPENFILE      : return (U32) OpenFile((LPFILEINFO) Parameter);
    case DF_FS_OPENNEXT      : return (U32) OpenNext((LPFATFILE) Parameter);
    case DF_FS_CLOSEFILE     : return (U32) CloseFile((LPFATFILE) Parameter);
    case DF_FS_DELETEFILE    : return DF_ERROR_NOTIMPL;
    case DF_FS_RENAMEFILE    : return DF_ERROR_NOTIMPL;
    case DF_FS_READ          : return (U32) ReadFile((LPFATFILE) Parameter);
    case DF_FS_WRITE         : return DF_ERROR_NOTIMPL;
  }

  return DF_ERROR_NOTIMPL;
}

/***************************************************************************/
