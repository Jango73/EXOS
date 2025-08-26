
// XFSMan.cpp

/*************************************************************************************************/

#include <windows.h>

#include <StdUtil.h>

#include "..\FileSys\FileSys.h"

/*************************************************************************************************/

extern "C"
{
  extern void* __cdecl Sector0;

  U32 __stdcall GetVXDEntry (U32);
}

/*************************************************************************************************/

char* TitleText =
  "EXOS File System Manager V1.0\n"
  "Copyright (c) 1999-2025 Jango73\n\n";

char* UsageText =
  "Usage :\n"
  "  XFSMan [command | options] \n"
  "\n"
  "Commands : \n"
  "  /fnnn  : Format using nnn bytes per cluster\n"
  "  /i     : Display drive information\n\n";

/*************************************************************************************************/

// DOS device types

#define DEVICE_FLOPPY_525_360KB 0x00
#define DEVICE_FLOPPY_525_1MB   0x01
#define DEVICE_FLOPPY_35_720KB  0x02
#define DEVICE_FLOPPY_8_SD      0x03
#define DEVICE_FLOPPY_8_DD      0x04
#define DEVICE_HARD             0x05
#define DEVICE_TAPE             0x06
#define DEVICE_UNKNOWN          0x07

/*************************************************************************************************/

#define VWIN32_DIOC_DOS_IOCTL 1

/*************************************************************************************************/

typedef struct tag_DEVIOCTL_REGISTERS
{
  DWORD reg_EBX;
  DWORD reg_EDX;
  DWORD reg_ECX;
  DWORD reg_EAX;
  DWORD reg_EDI;
  DWORD reg_ESI;
  DWORD reg_Flags; 
} DEVIOCTL_REGISTERS, *pDEVIOCTL_REGISTERS;

/*************************************************************************************************/

typedef struct tag_MID
{
  WORD  midInfoLevel;
  DWORD midSerialNum;
  BYTE  midVolLabel[11];
  BYTE  midFileSysType[8];
} MID, *pMID;

/*************************************************************************************************/

typedef struct tag_DEVIOCTL_PARAMS
{
  U8  Flags;
  U8  DeviceType;
  U16 Attributes;
  U16 NumCylinders;
  U8  MediaType;
  U16 BytesPerSector;
  U8  SectorsPerCluster;
  U16 NumReservedSectors;
  U8  NumFAT;
  U16 MaxFilesInRoot;
  U16 NumSectors;
  U8  MediaDescriptor;
  U16 SectorsPerFat;
  U16 SectorsPerTrack;
  U16 NumHeads;
  U32 HiddenSectors;
} DEVIOCTL_PARAMS, *pDEVIOCTL_PARAMS;

/*************************************************************************************************/

typedef struct tag_DEVIOCTL_COMMAND
{
  U8  Res1;
  U16 NumHeads;
  U16 NumCylinders;
  U16 Sector;
  U16 NumSectors;
  U32 Buffer;
} DEVIOCTL_COMMAND, *pDEVIOCTL_COMMAND;

/*************************************************************************************************/

// Serial            : 1BF6-2149
// Total bytes       : 1,457,664
// Bytes per cluster :       512
// Total clusters    :     2,847

/*************************************************************************************************/

  class XFS_Drive
  {

    public :

      XFS_Drive (const String&);
      virtual ~XFS_Drive ();

      int Format      (U32);
      int DisplayInfo ();

    public :

      String             DriveLetter;
      BOOL               Valid;
      DeviceControlBlock Control;

  };

/*************************************************************************************************/

#define COM_FORMAT 0x0001
#define COM_INFO   0x0002

U32   Command          = 0;
U32   DisplayInfo      = 0;
U32   ClusterSize      = 1024;

char TargetDrive [32];

/*************************************************************************************************/

/*
U64 DivU64 (U64 Left, U64 Right)
{
  U64 foo = { 0, 0 };
  return foo;
}
*/

/*************************************************************************************************/

int LogRegisters (DEVIOCTL_REGISTERS* IORegs)
{
  char szTemp [16];
  sprintf(szTemp, "%X", (U32) IORegs->reg_EAX);   OutStream << "EAX   : " << szTemp << EndL;
  sprintf(szTemp, "%X", (U32) IORegs->reg_EBX);   OutStream << "EBX   : " << szTemp << EndL;
  sprintf(szTemp, "%X", (U32) IORegs->reg_ECX);   OutStream << "ECX   : " << szTemp << EndL;
  sprintf(szTemp, "%X", (U32) IORegs->reg_EDX);   OutStream << "EDX   : " << szTemp << EndL;
  sprintf(szTemp, "%X", (U32) IORegs->reg_ESI);   OutStream << "ESI   : " << szTemp << EndL;
  sprintf(szTemp, "%X", (U32) IORegs->reg_EDI);   OutStream << "EDI   : " << szTemp << EndL;

  OutStream << "Flags : CF PF AF ZF SF OF" << EndL;
  OutStream << "        ";
  OutStream << (IORegs->reg_Flags & 0x0001 ? "1" : "0") << "  ";
  OutStream << (IORegs->reg_Flags & 0x0004 ? "1" : "0") << "  ";
  OutStream << (IORegs->reg_Flags & 0x0010 ? "1" : "0") << "  ";
  OutStream << (IORegs->reg_Flags & 0x0040 ? "1" : "0") << "  ";
  OutStream << (IORegs->reg_Flags & 0x0080 ? "1" : "0") << "  ";
  OutStream << (IORegs->reg_Flags & 0x0800 ? "1" : "0") << EndL;

  return 1;
}

/*************************************************************************************************/

int DoIOControl (DEVIOCTL_REGISTERS* IORegs)
{
  DWORD Bytes;

  // IORegs->reg_Flags = 0x8000;

  HANDLE hDevice = CreateFile
  (
    "\\\\.\\vwin32",
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    (LPSECURITY_ATTRIBUTES) NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    (HANDLE) NULL
  );

  if (hDevice == (HANDLE) INVALID_HANDLE_VALUE) return 0;

  BOOL bResult = DeviceIoControl
  (
    hDevice, VWIN32_DIOC_DOS_IOCTL,
    IORegs, sizeof(*IORegs),
    IORegs, sizeof(*IORegs),
    &Bytes, 0
  );

  CloseHandle(hDevice);

  if (bResult == FALSE || IORegs->reg_Flags & 0x0001) return 0;

  return 1;
}

/*************************************************************************************************/

int WriteBootSector (pBlockDevice Device, pVoid Buffer)
{
  DEVIOCTL_COMMAND   IOCommand;
  DEVIOCTL_REGISTERS IORegs;

  IOCommand.NumHeads     = 0;
  IOCommand.NumCylinders = 0;
  IOCommand.Sector       = 0;
  IOCommand.NumSectors   = 1;
  IOCommand.Buffer       = (U32) Buffer;

  IORegs.reg_EAX   = 0x0000440D;
  IORegs.reg_EBX   = Device->ID;
  IORegs.reg_ECX   = 0x00000841;
  IORegs.reg_EDX   = (DWORD) &IOCommand;
  IORegs.reg_ESI   = 0;
  IORegs.reg_EDI   = 0;
  IORegs.reg_Flags = 0;

  if (DoIOControl(&IORegs) == FALSE)
  {
    OutStream << "DOS IO control error : ";

    switch (IORegs.reg_EAX)
    {
      case 0x0001 : OutStream << "Incorrect function";  break;
      case 0x0002 : OutStream << "Incorrect disk unit"; break;
      case 0x0006 : OutStream << "Incorrect unit";      break;
      default     : OutStream << "Unknown";             break;
    }

    OutStream << EndL;
    OutStream << "Registers : " << EndL;
    LogRegisters(&IORegs);

    return 0;
  }

/*
  union  REGS  Regs;
  struct SREGS SRegs;

  Regs.h.ah = 0x03;
  Regs.h.al = 0x01;
  Regs.h.ch = 0x00;
  Regs.h.cl = 0x00;
  Regs.h.dh = 0x00;
  Regs.h.dl = 0x00; // A:
  Regs.w.bx = (unsigned) Buffer;

  SRegs.es = FP_SEG(Buffer);

  int386x(0x13, &Regs, &Regs, &SRegs);
*/

  return 1;
}

/*************************************************************************************************/

// Max addressable byte with 512 byte sector and 32-bit sector value : 2,199,023,255,551

U32 XFS_GetClusterLocation (U32 Cluster, pDeviceControlBlock Control, pBlockDeviceLocation Location)
{
  if (Control  == NULL) return 0;
  if (Location == NULL) return 0;

  if (Control->Device.TracksPerCylinder == 0) return 0;
  if (Control->Device.SectorsPerTrack   == 0) return 0;
  if (Control->Device.BytesPerSector    == 0) return 0;

  U32 NumSystemSectors = XFS_SYSTEM_CLUSTER_SIZE / Control->Device.BytesPerSector;
  if (NumSystemSectors < 1) NumSystemSectors = 1;

  // We have 2 system sectors (Boot sector and SuperBlock)
  NumSystemSectors *= 2;

  // Get the sector number from Cluster and Control
  U32 NumSectorsPerCluster = Control->Super.ClusterSize / Control->Device.BytesPerSector;
  U32 AbsoluteSector       = (Cluster * NumSectorsPerCluster) + NumSystemSectors;
  U32 AbsoluteTrack        = AbsoluteSector / Control->Device.SectorsPerTrack;
  U32 AbsoluteCylinder     = AbsoluteTrack  / Control->Device.TracksPerCylinder;

  Location->Cylinder = AbsoluteCylinder;
  Location->Track    = AbsoluteTrack  % Control->Device.TracksPerCylinder;
  Location->Sector   = AbsoluteSector % Control->Device.SectorsPerTrack;

  return 1;
}

/*************************************************************************************************/

U32 XFS_WriteSuperBlock (pDeviceControlBlock Control)
{
  DEVIOCTL_COMMAND   IOCommand;
  DEVIOCTL_REGISTERS IORegs;

/*
  IOCommand.Sector     = Control->Device.SuperBlockLocation.Sector;
  IOCommand.NumSectors = Control->Device.SuperBlockLocation.NumSectors;
  IOCommand.Buffer     = (U32) Block;

  IORegs.reg_EAX = 0x440D;
  IORegs.reg_EBX = Control->Device.ID;
  IORegs.reg_ECX = 0x0841;
  IORegs.reg_EDX = (DWORD) &IOCommand;

  if (DoIOControl(&IORegs) == FALSE) return 0;
*/

  return 1;
}

/*************************************************************************************************/

int XFS_InitSuperBlock (pSuperBlock Block)
{
  if (Block == NULL) return 0;

  memset(Block, 0, sizeof(SuperBlock));

  char* EXOSMagic = "EXOS";

  Block->EXOSMagic    = (*((U32*) EXOSMagic));
  Block->CreatorMagic = (*((U32*) EXOSMagic));
  Block->Version      = (0x0001 << 16) | (0x0000);

  return 1;
}

/*************************************************************************************************/

int XFS_CreateSuperBlock (pDeviceControlBlock Control, U32 Cluster_ByteSize)
{
  if (Control->Device.BytesPerSector == 0) return 0;

  U32 SystemBlock_NumSectors = XFS_SYSTEM_CLUSTER_SIZE / Control->Device.BytesPerSector;
  if (SystemBlock_NumSectors < 1) SystemBlock_NumSectors = 1;

  // Make sure the cluster size is not lower than the sector size
  if (Cluster_ByteSize < Control->Device.BytesPerSector) Cluster_ByteSize = Control->Device.BytesPerSector;

  // Compute the total number of clusters
  // U32 Disc_NumClusters = 1438;
  U32 Disc_NumClusters = Control->Device.Capacity.LO / Cluster_ByteSize;

  U32 NumFileRecs = 256;

  // The cluster bitmap is at cluster 0
  U32 ClusterBitmap_Cluster     = 0;
  U32 ClusterBitmap_ByteSize    = Disc_NumClusters / 8;
  U32 ClusterBitmap_NumClusters = ClusterBitmap_ByteSize / Cluster_ByteSize;

  if (ClusterBitmap_NumClusters < 1) ClusterBitmap_NumClusters = Cluster_ByteSize;

  // The file table follows the cluster bitmap
  U32 FileTable_Cluster     = ClusterBitmap_Cluster + ClusterBitmap_NumClusters;
  U32 FileTable_ByteSize    = NumFileRecs * sizeof(FileRec);
  U32 FileTable_NumClusters = FileTable_ByteSize / Cluster_ByteSize;

  if (FileTable_NumClusters < 1) FileTable_NumClusters = Cluster_ByteSize;

  // The file table bitmap follows the file table
  U32 FileBitmap_Cluster     = FileTable_Cluster + FileTable_NumClusters;
  U32 FileBitmap_ByteSize    = FileTable_NumClusters / 8;
  U32 FileBitmap_NumClusters = FileBitmap_ByteSize / Cluster_ByteSize;

  if (FileBitmap_NumClusters < 1) FileBitmap_NumClusters = Cluster_ByteSize;

  // Fill the SuperBlock
  Control->Super.ClusterSize     = Cluster_ByteSize;
  Control->Super.ClusterBitmap   = ClusterBitmap_Cluster;
  Control->Super.NumClusters     = Disc_NumClusters;
  Control->Super.NumFreeClusters = 0;
  Control->Super.FileTable       = FileTable_Cluster;
  Control->Super.FileBitmap      = FileBitmap_Cluster;
  Control->Super.NumFiles        = NumFileRecs;
  Control->Super.NumFreeFiles    = NumFileRecs;
  Control->Super.File_OS         = 0;
  Control->Super.File_Root       = 0;
  Control->Super.MaxMountCount   = 1024;
  Control->Super.MountCount      = 0;

  // Location of the boot block
  Control->Location_BootCluster.Cylinder   = 0;
  Control->Location_BootCluster.Head       = 0;
  Control->Location_BootCluster.Track      = 0;
  Control->Location_BootCluster.Sector     = 0;
  Control->Location_BootCluster.NumSectors = SystemBlock_NumSectors;

  // Location of the SuperBlock
  Control->Location_SuperBlock.Cylinder   = 0;
  Control->Location_SuperBlock.Head       = 0;
  Control->Location_SuperBlock.Track      = 0;
  Control->Location_SuperBlock.Sector     = SystemBlock_NumSectors;
  Control->Location_SuperBlock.NumSectors = SystemBlock_NumSectors;

  if (!XFS_GetClusterLocation(Control->Super.ClusterBitmap, Control, &(Control->Location_ClusterBitmap))) return 0;
  if (!XFS_GetClusterLocation(Control->Super.FileTable,     Control, &(Control->Location_FileTable)))     return 0;
  if (!XFS_GetClusterLocation(Control->Super.FileBitmap,    Control, &(Control->Location_FileBitmap)))    return 0;

  return 1;
}

/*************************************************************************************************/

U32 XFS_SuperBlockToString(pSuperBlock Block, char* Text)
{
  char szTemp [64];

  Text[0] = '\0';

  strcat(Text, "Magic number : ");
  *((U32*)szTemp) = Block->EXOSMagic; szTemp[4] = '\0';
  strcat(Text, szTemp);
  strcat(Text, "\n");

  sprintf(szTemp, "Version : %u.%u\n",
          (unsigned) ((Block->Version & 0xFFFF0000) >> 16), (unsigned) (Block->Version & 0x0000FFFF));
  strcat(Text, szTemp);

  sprintf(szTemp, "Cluster size : %u\n", Block->ClusterSize);
  strcat(Text, szTemp);

  sprintf(szTemp, "Cluster bitmap : %u\n", Block->ClusterBitmap);
  strcat(Text, szTemp);

  sprintf(szTemp, "Number of clusters : %u\n", Block->NumClusters);
  strcat(Text, szTemp);

  sprintf(szTemp, "Number of free clusters : %u\n", Block->NumFreeClusters);
  strcat(Text, szTemp);

  sprintf(szTemp, "File record table : %u\n", Block->FileTable);
  strcat(Text, szTemp);

  sprintf(szTemp, "File record bitmap : %u\n", Block->FileBitmap);
  strcat(Text, szTemp);

  sprintf(szTemp, "Number of file records : %u\n", Block->NumFiles);
  strcat(Text, szTemp);

  sprintf(szTemp, "Number of free file records : %u\n", Block->NumFreeFiles);
  strcat(Text, szTemp);

  sprintf(szTemp, "OS file record : %u\n", Block->File_OS);
  strcat(Text, szTemp);

  sprintf(szTemp, "Root file record : %u\n", Block->File_Root);
  strcat(Text, szTemp);

  strcat(Text, "Creator magic number : ");
  *((U32*)szTemp) = Block->CreatorMagic; szTemp[4] = '\0';
  strcat(Text, szTemp);
  strcat(Text, "\n");

  sprintf(szTemp, "Maximum mount count : %u\n", Block->MaxMountCount);
  strcat(Text, szTemp);

  sprintf(szTemp, "Current mount count : %u\n", Block->MountCount);
  strcat(Text, szTemp);

  return 1;
}

/*************************************************************************************************/

U32 XFS_BlockDeviceLocationToString (pBlockDeviceLocation BlockLocation, char* Text)
{
  char szTemp [64];

  Text[0] = '\0';

  sprintf(szTemp, "Cylinder : %u\n", BlockLocation->Cylinder);
  strcat(Text, szTemp);

  sprintf(szTemp, "Track : %u\n", BlockLocation->Track);
  strcat(Text, szTemp);

  sprintf(szTemp, "Sector : %u\n", BlockLocation->Sector);
  strcat(Text, szTemp);

  return 1;
}

/*************************************************************************************************/

XFS_Drive :: XFS_Drive (const String& NewDriveLetter)
{
  Valid       = 0;
  DriveLetter = NewDriveLetter;

  DriveLetter.ToUpperCase();

  LONG DriveNumber = ((LONG) DriveLetter[0] - (LONG) 'A') + 1;

  if (DriveNumber < 1 || DriveNumber > 26) return;

  /***********************************/

  DEVIOCTL_REGISTERS IORegs;
  DEVIOCTL_PARAMS    Params;

  IORegs.reg_EAX = 0x440D;
  IORegs.reg_EBX = DriveNumber;
  IORegs.reg_ECX = 0x0860;
  IORegs.reg_EDX = (DWORD) &Params;

  if (DoIOControl(&IORegs) == FALSE) return;

  Valid = 1;

  /***********************************/

  Control.Device.ID                    = DriveNumber;
  Control.Device.DeviceType            = 0;
  Control.Device.Cylinders             = 2;
  Control.Device.Heads                 = 0;
  Control.Device.Sectors               = Params.NumSectors;
  Control.Device.TracksPerCylinder     = Params.NumCylinders;
  Control.Device.SectorsPerTrack       = Params.SectorsPerTrack;
  Control.Device.BytesPerSector        = Params.BytesPerSector;
  Control.Device.Capacity.LO           = Params.NumSectors * Params.BytesPerSector;
  Control.Device.Capacity.HI           = 0;
}

/*************************************************************************************************/

XFS_Drive :: ~XFS_Drive ()
{
}

/*************************************************************************************************/

int XFS_Drive :: Format (U32 ClusterSize)
{
  OutStream << "Formatting drive " << DriveLetter << " with a cluster size of " << ClusterSize << EndL << EndL;

  if (XFS_InitSuperBlock(&(Control.Super))        == 0) return 0;
  if (XFS_CreateSuperBlock(&Control, ClusterSize) == 0) return 0;

  OutStream << "Writing boot sector..." << EndL;

  if (WriteBootSector(&(Control.Device), (pVoid) Sector0) == 0)
  {
    OutStream << "Could not write boot sector" << EndL;
    return 0;
  }

  OutStream << "Done" << EndL << EndL;

  char szTemp [2048];

  OutStream << "SuperBlock :" << EndL;
  XFS_SuperBlockToString(&(Control.Super), szTemp);
  OutStream << szTemp << EndL;

  OutStream << "SuperBlock physical location :" << EndL;
  XFS_BlockDeviceLocationToString(&(Control.Location_SuperBlock), szTemp);
  OutStream << szTemp << EndL;

  OutStream << "Cluster bitmap physical location :" << EndL;
  XFS_BlockDeviceLocationToString(&(Control.Location_ClusterBitmap), szTemp);
  OutStream << szTemp << EndL;

  OutStream << "File table physical location :" << EndL;
  XFS_BlockDeviceLocationToString(&(Control.Location_FileTable), szTemp);
  OutStream << szTemp << EndL;

  OutStream << "File bitmap physical location :" << EndL;
  XFS_BlockDeviceLocationToString(&(Control.Location_FileBitmap), szTemp);
  OutStream << szTemp << EndL;

  return 1;
}

/*************************************************************************************************/

int XFS_Drive :: DisplayInfo ()
{
  OutStream << "Drive number        : " << Control.Device.ID                << EndL;
  OutStream << "Device type         : " << Control.Device.DeviceType        << EndL;
  OutStream << "Number of cylinders : " << Control.Device.Cylinders         << EndL;
  OutStream << "Number of heads     : " << Control.Device.Heads             << EndL;
  OutStream << "Number of sectors   : " << Control.Device.Sectors           << EndL;
  OutStream << "Tracks per cylinder : " << Control.Device.TracksPerCylinder << EndL;
  OutStream << "Sectors per track   : " << Control.Device.SectorsPerTrack   << EndL;
  OutStream << "Bytes per sector    : " << Control.Device.BytesPerSector    << EndL;
  OutStream << "Capacity            : " << Control.Device.Capacity.LO       << EndL;

  return 1;
}

/*************************************************************************************************/

int ParseOptions (long NumOptions, char** Options)
{
  for (long c = 0; c < NumOptions; c++)
  {
    if (Options[c][0] == '-' || Options[c][0] == '/')
    {
      switch (Options[c][1])
      {
        case 'f' :
        case 'F' :
        {
          Command = COM_FORMAT;
          if (Options[c][2] != '\0')
          {
            ClusterSize = atol(Options[c] + 2);
            XFS_SETVALIDCLUSTERSIZE(ClusterSize);
          }
        }
        break;
        case 'i' :
        case 'I' :
        {
          Command = COM_INFO;
        }
        break;
      }
    }
    else
    {
      if (Options[c][1] == ':')
      {
        strcpy(TargetDrive, Options[c]);
      }
    }
  }

  return 1;
}

/*************************************************************************************************/

int main (int argc, char** argv)
{
  OutStream << TitleText;

  if (argc < 2)
  {
    OutStream << UsageText;
    return 0;
  }

  ParseOptions(argc, argv);

  if (Command == 0)
  {
    OutStream << "No command given" << EndL;
    return 1;
  }

  switch (Command)
  {
    case COM_FORMAT :
    {
      // Force floppy drive for security
      strcpy(TargetDrive, "A:");

      XFS_Drive Drive(TargetDrive);
      if (Drive.Valid == FALSE) return 1;

      Drive.Format(ClusterSize);
    }
    break;
    case COM_INFO :
    {
      XFS_Drive Drive(TargetDrive);
      if (Drive.Valid == FALSE) return 1;

      Drive.DisplayInfo();
    }
    break;
  }

  return 0;
}

/*************************************************************************************************/

#if 0

void main ()
{

  HANDLE hFile = CreateFile
  (
    "\\.\A:",
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );

  if (hFile)
  {
    DISK_GEOMETRY Geometry;

    BOOL Res = DeviceIOControl
    (
      hFile,
      IOCTL_DISK_GET_DRIVE_GEOMETRY,
      NULL, 0,
      (LPVOID) &Geometry, sizeof(Geometry),
      &Bytes, NULL
    );

    CloseHandle(hFile);

    if (Res)
    {
      OutStream << "Tracks per cylinder : " << Geometry.TracksPerCylinder;
      OutStream << "Sectors per track   : " << Geometry.SectorsPerTrack;
      OutStream << "Bytes per sector    : " << Geometry.BytesPerSector;
    }
  }

}

#endif

/*************************************************************************************************/
