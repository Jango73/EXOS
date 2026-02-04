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


    NTFS Helpers

\************************************************************************/

#include "drivers/NTFS.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"

/***************************************************************************/

#define NTFS_VER_MAJOR 1
#define NTFS_VER_MINOR 0
#define NTFS_MAX_SECTOR_SIZE 4096
#define NTFS_MIN_FILE_RECORD_SIZE 512
#define NTFS_MAX_FILE_RECORD_SIZE 4096

/***************************************************************************/

typedef struct tag_NTFSFILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    NTFS_MBR BootSector;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BytesPerSector;
    U32 SectorsPerCluster;
    U32 BytesPerCluster;
    U64 MftStartCluster;
    STR VolumeLabel[MAX_FS_LOGICAL_NAME];
} NTFSFILESYSTEM, *LPNTFSFILESYSTEM;

/***************************************************************************/

static const U8 NtfsDaysPerMonth[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/***************************************************************************/

/**
 * @brief Returns TRUE for supported disk sector sizes.
 *
 * @param BytesPerSector Logical bytes per sector.
 * @return TRUE for supported sizes, FALSE otherwise.
 */
static BOOL NtfsIsSupportedSectorSize(U32 BytesPerSector) {
    return (BytesPerSector == 512) || (BytesPerSector == 4096);
}

/***************************************************************************/

/**
 * @brief Determines whether a value is a power of two.
 *
 * @param Value Input value.
 * @return TRUE when Value is a power of two, FALSE otherwise.
 */
static BOOL NtfsIsPowerOfTwo(U32 Value) {
    if (Value == 0) return FALSE;
    return (Value & (Value - 1)) == 0;
}

/***************************************************************************/

/**
 * @brief Query logical bytes per sector from a storage unit.
 *
 * @param Disk Target storage unit.
 * @return Bytes per sector, or 0 when unavailable.
 */
static U32 NtfsGetDiskBytesPerSector(LPSTORAGE_UNIT Disk) {
    DISKINFO DiskInfo;
    U32 Result;

    if (Disk == NULL || Disk->Driver == NULL) return 0;

    MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
    DiskInfo.Disk = Disk;
    Result = Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);
    if (Result != DF_RETURN_SUCCESS) return 0;

    return DiskInfo.BytesPerSector;
}

/***************************************************************************/

/**
 * @brief Reads a partition boot sector.
 *
 * @param Disk Target disk.
 * @param BootSectorLba Boot sector LBA.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param BytesPerSectorOut Optional output for detected sector size.
 * @return TRUE on success, FALSE on read/validation failure.
 */
static BOOL NtfsReadBootSector(
    LPSTORAGE_UNIT Disk, SECTOR BootSectorLba, LPVOID Buffer, U32 BufferSize, U32* BytesPerSectorOut) {
    IOCONTROL Control;
    U32 BytesPerSector;
    U32 Result;

    if (BytesPerSectorOut != NULL) *BytesPerSectorOut = 0;
    if (Disk == NULL || Disk->Driver == NULL || Buffer == NULL) return FALSE;

    BytesPerSector = NtfsGetDiskBytesPerSector(Disk);
    if (!NtfsIsSupportedSectorSize(BytesPerSector)) {
        WARNING(TEXT("[NtfsReadBootSector] Unsupported sector size %u"), BytesPerSector);
        return FALSE;
    }

    if (BytesPerSector > BufferSize || BytesPerSector > NTFS_MAX_SECTOR_SIZE) {
        WARNING(TEXT("[NtfsReadBootSector] Buffer too small for sector size %u"), BytesPerSector);
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = BootSectorLba;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = BytesPerSector;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("[NtfsReadBootSector] Boot sector read failed result=%x"), Result);
        return FALSE;
    }

    if (BytesPerSectorOut != NULL) *BytesPerSectorOut = BytesPerSector;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Fills generic volume information for NTFS.
 *
 * @param VolumeInfo Output volume information.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static U32 NtfsGetVolumeInfo(LPVOLUMEINFO VolumeInfo) {
    LPFILESYSTEM Header;
    LPNTFSFILESYSTEM FileSystem;

    if (VolumeInfo == NULL || VolumeInfo->Size != sizeof(VOLUMEINFO)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Header = (LPFILESYSTEM)VolumeInfo->Volume;
    SAFE_USE_VALID_ID(Header, KOID_FILESYSTEM) {
        FileSystem = (LPNTFSFILESYSTEM)Header;
        if (!StringEmpty(FileSystem->VolumeLabel)) {
            StringCopy(VolumeInfo->Name, FileSystem->VolumeLabel);
        } else {
            StringCopy(VolumeInfo->Name, FileSystem->Header.Name);
        }
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief Dispatch entry point for the NTFS driver.
 *
 * @param Function Requested function ID.
 * @param Parameter Optional function parameter.
 * @return DF_RETURN_* status code.
 */
static UINT NTFSCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(NTFS_VER_MAJOR, NTFS_VER_MINOR);
        case DF_FS_GETVOLUMEINFO:
            return NtfsGetVolumeInfo((LPVOLUMEINFO)Parameter);
        case DF_FS_SETVOLUMEINFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_OPENFILE:
            return (UINT)NULL;
        default:
            return DF_RETURN_NOT_IMPLEMENTED;
    }
}

/***************************************************************************/

DRIVER DATA_SECTION NTFSDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = NTFS_VER_MAJOR,
    .VersionMinor = NTFS_VER_MINOR,
    .Designer = "Microsoft Corporation",
    .Manufacturer = "Microsoft Corporation",
    .Product = "NTFS File System",
    .Command = NTFSCommands};

/***************************************************************************/

/**
 * @brief Mount an NTFS partition and cache boot geometry.
 *
 * @param Disk Physical disk.
 * @param Partition Partition descriptor.
 * @param Base Base LBA offset.
 * @param PartIndex Partition index.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL MountPartition_NTFS(LPSTORAGE_UNIT Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[NTFS_MAX_SECTOR_SIZE];
    LPNTFS_MBR BootSector;
    LPNTFSFILESYSTEM FileSystem;
    U32 DiskBytesPerSector;
    U32 BootBytesPerSector;
    U32 SectorsPerCluster;
    U32 BytesPerCluster;
    U64 MftStartCluster;
    SECTOR PartitionStart;

    if (Disk == NULL || Partition == NULL) return FALSE;

    PartitionStart = Base + Partition->LBA;
    if (!NtfsReadBootSector(Disk, PartitionStart, Buffer, sizeof(Buffer), &DiskBytesPerSector)) {
        return FALSE;
    }

    if (Buffer[510] != 0x55 || Buffer[511] != 0xAA) {
        WARNING(TEXT("[MountPartition_NTFS] Invalid boot signature (%x, %x)"),
            Buffer[510], Buffer[511]);
        return FALSE;
    }

    BootSector = (LPNTFS_MBR)Buffer;
    if (BootSector->OEMName[0] != 'N' || BootSector->OEMName[1] != 'T' ||
        BootSector->OEMName[2] != 'F' || BootSector->OEMName[3] != 'S') {
        WARNING(TEXT("[MountPartition_NTFS] Invalid OEM name (%x %x %x %x %x %x %x %x)"),
            BootSector->OEMName[0], BootSector->OEMName[1],
            BootSector->OEMName[2], BootSector->OEMName[3],
            BootSector->OEMName[4], BootSector->OEMName[5],
            BootSector->OEMName[6], BootSector->OEMName[7]);
        return FALSE;
    }

    BootBytesPerSector = BootSector->BytesPerSector;
    if (!NtfsIsSupportedSectorSize(BootBytesPerSector)) {
        WARNING(TEXT("[MountPartition_NTFS] Unsupported boot sector size %u"), BootBytesPerSector);
        return FALSE;
    }

    if (BootBytesPerSector != DiskBytesPerSector) {
        WARNING(TEXT("[MountPartition_NTFS] Disk/boot sector mismatch %u/%u"),
            DiskBytesPerSector, BootBytesPerSector);
        return FALSE;
    }

    SectorsPerCluster = BootSector->SectorsPerCluster;
    if (!NtfsIsPowerOfTwo(SectorsPerCluster)) {
        WARNING(TEXT("[MountPartition_NTFS] Invalid sectors per cluster %u"), SectorsPerCluster);
        return FALSE;
    }

    BytesPerCluster = BootBytesPerSector * SectorsPerCluster;
    if (BytesPerCluster == 0) {
        WARNING(TEXT("[MountPartition_NTFS] Invalid bytes per cluster"));
        return FALSE;
    }

    MftStartCluster = BootSector->LCN_VCN0_MFT;

    FileSystem = (LPNTFSFILESYSTEM)CreateKernelObject(sizeof(NTFSFILESYSTEM), KOID_FILESYSTEM);
    if (FileSystem == NULL) {
        ERROR(TEXT("[MountPartition_NTFS] Unable to allocate NTFS filesystem object"));
        return FALSE;
    }

    InitMutex(&(FileSystem->Header.Mutex));
    FileSystem->Header.Driver = &NTFSDriver;
    FileSystem->Header.StorageUnit = Disk;
    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    FileSystem->Disk = Disk;
    MemoryCopy(&(FileSystem->BootSector), BootSector, sizeof(NTFS_MBR));
    FileSystem->PartitionStart = PartitionStart;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BytesPerSector = BootBytesPerSector;
    FileSystem->SectorsPerCluster = SectorsPerCluster;
    FileSystem->BytesPerCluster = BytesPerCluster;
    FileSystem->MftStartCluster = MftStartCluster;
    StringClear(FileSystem->VolumeLabel);

    ListAddItem(GetFileSystemList(), FileSystem);

    DEBUG(TEXT("[MountPartition_NTFS] Mounted %s bytes_per_sector=%u sectors_per_cluster=%u mft_cluster=%x%08x"),
        FileSystem->Header.Name,
        FileSystem->BytesPerSector,
        FileSystem->SectorsPerCluster,
        (U32)U64_High32(FileSystem->MftStartCluster),
        (U32)U64_Low32(FileSystem->MftStartCluster));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve geometry cached at NTFS mount time.
 *
 * @param FileSystem Mounted filesystem pointer.
 * @param Geometry Destination geometry.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsGetVolumeGeometry(LPFILESYSTEM FileSystem, LPNTFS_VOLUME_GEOMETRY Geometry) {
    LPNTFSFILESYSTEM NtfsFileSystem;

    if (FileSystem == NULL || Geometry == NULL) return FALSE;
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        Geometry->BytesPerSector = NtfsFileSystem->BytesPerSector;
        Geometry->SectorsPerCluster = NtfsFileSystem->SectorsPerCluster;
        Geometry->BytesPerCluster = NtfsFileSystem->BytesPerCluster;
        Geometry->MftStartCluster = NtfsFileSystem->MftStartCluster;
        StringCopy(Geometry->VolumeLabel, NtfsFileSystem->VolumeLabel);

        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Determine whether a year is leap.
 *
 * @param Year Year value.
 * @return TRUE for leap years, FALSE otherwise.
 */
static BOOL NtfsIsLeapYear(U32 Year) {
    return (Year % 400 == 0) || ((Year % 4 == 0) && (Year % 100 != 0));
}

/***************************************************************************/

/**
 * @brief Return number of days in a month for a given year.
 *
 * @param Year Year value.
 * @param Month Month value in range 1..12.
 * @return Number of days for the requested month.
 */
static U32 NtfsGetDaysInMonth(U32 Year, U32 Month) {
    U32 Days = NtfsDaysPerMonth[Month - 1];

    if (Month == 2 && NtfsIsLeapYear(Year)) {
        Days++;
    }

    return Days;
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one common year.
 *
 * @return Number of 100ns intervals in 365 days.
 */
static U64 NtfsTicksPerCommonYear(void) {
    return U64_Make(0x00011ED1, 0x78C6C000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one leap year.
 *
 * @return Number of 100ns intervals in 366 days.
 */
static U64 NtfsTicksPerLeapYear(void) {
    return U64_Make(0x00011F9A, 0xA3308000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one day.
 *
 * @return Number of 100ns intervals in one day.
 */
static U64 NtfsTicksPerDay(void) {
    return U64_Make(0x000000C9, 0x2A69C000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one hour.
 *
 * @return Number of 100ns intervals in one hour.
 */
static U64 NtfsTicksPerHour(void) {
    return U64_Make(0x00000008, 0x61C46800);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one minute.
 *
 * @return Number of 100ns intervals in one minute.
 */
static U64 NtfsTicksPerMinute(void) {
    return U64_Make(0x00000000, 0x23C34600);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one second.
 *
 * @return Number of 100ns intervals in one second.
 */
static U64 NtfsTicksPerSecond(void) {
    return U64_Make(0x00000000, 0x00989680);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one millisecond.
 *
 * @return Number of 100ns intervals in one millisecond.
 */
static U64 NtfsTicksPerMillisecond(void) {
    return U64_Make(0x00000000, 0x00002710);
}

/***************************************************************************/

/**
 * @brief Convert an NTFS timestamp to DATETIME.
 *
 * NTFS timestamps use 100-nanosecond intervals since January 1st, 1601.
 *
 * @param NtfsTimestamp Timestamp value from NTFS metadata.
 * @param DateTime Destination DATETIME structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL NtfsTimestampToDateTime(U64 NtfsTimestamp, LPDATETIME DateTime) {
    U32 Year = 1601;
    U32 Month = 1;
    U32 Day = 1;
    U32 Hour = 0;
    U32 Minute = 0;
    U32 Second = 0;
    U32 Milli = 0;
    U32 DayIndex = 0;
    U64 RemainingTicks;

    if (DateTime == NULL) return FALSE;

    MemorySet(DateTime, 0, sizeof(DATETIME));
    RemainingTicks = NtfsTimestamp;

    while (TRUE) {
        U64 YearTicks = NtfsIsLeapYear(Year) ? NtfsTicksPerLeapYear() : NtfsTicksPerCommonYear();
        if (U64_Cmp(RemainingTicks, YearTicks) < 0) {
            break;
        }
        RemainingTicks = U64_Sub(RemainingTicks, YearTicks);
        Year++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerDay()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerDay());
        DayIndex++;
    }

    Month = 1;
    while (Month <= 12) {
        U32 DaysInMonth = NtfsGetDaysInMonth(Year, Month);
        if (DayIndex < DaysInMonth) {
            Day = DayIndex + 1;
            break;
        }
        DayIndex -= DaysInMonth;
        Month++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerHour()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerHour());
        Hour++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerMinute()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerMinute());
        Minute++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerSecond()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerSecond());
        Second++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerMillisecond()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerMillisecond());
        Milli++;
    }

    DateTime->Year = Year;
    DateTime->Month = Month;
    DateTime->Day = Day;
    DateTime->Hour = Hour;
    DateTime->Minute = Minute;
    DateTime->Second = Second;
    DateTime->Milli = Milli;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Load a U16 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
static U16 NtfsLoadU16(LPCVOID Address) {
    U16 Value;

    MemoryCopy(&Value, Address, sizeof(U16));
    return Value;
}

/***************************************************************************/

/**
 * @brief Store a U16 to an arbitrary memory address.
 *
 * @param Address Destination address.
 * @param Value Value to store.
 */
static void NtfsStoreU16(LPVOID Address, U16 Value) {
    MemoryCopy(Address, &Value, sizeof(U16));
}

/***************************************************************************/

/**
 * @brief Shift a U64 value left by one bit.
 *
 * @param Value Input value.
 * @return Shifted value.
 */
static U64 NtfsU64ShiftLeft1(U64 Value) {
    U32 High;
    U32 Low;

    High = U64_High32(Value);
    Low = U64_Low32(Value);

    return U64_Make((High << 1) | (Low >> 31), Low << 1);
}

/***************************************************************************/

/**
 * @brief Shift a U64 value right by one bit.
 *
 * @param Value Input value.
 * @return Shifted value.
 */
static U64 NtfsU64ShiftRight1(U64 Value) {
    U32 High;
    U32 Low;

    High = U64_High32(Value);
    Low = U64_Low32(Value);

    return U64_Make(High >> 1, (Low >> 1) | ((High & 1) << 31));
}

/***************************************************************************/

/**
 * @brief Multiply two U32 values and return a U64 product.
 *
 * @param Left Left factor.
 * @param Right Right factor.
 * @return 64-bit product.
 */
static U64 NtfsMultiplyU32ToU64(U32 Left, U32 Right) {
    U64 Result = U64_Make(0, 0);
    U64 Addend = U64_FromU32(Left);

    while (Right != 0) {
        if ((Right & 1) != 0) {
            Result = U64_Add(Result, Addend);
        }
        Right >>= 1;
        if (Right != 0) {
            Addend = NtfsU64ShiftLeft1(Addend);
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Shift a U64 value right by N bits.
 *
 * @param Value Input value.
 * @param Shift Number of bits to shift.
 * @return Shifted value.
 */
static U64 NtfsU64ShiftRight(U64 Value, U32 Shift) {
    while (Shift > 0) {
        Value = NtfsU64ShiftRight1(Value);
        Shift--;
    }
    return Value;
}

/***************************************************************************/

/**
 * @brief Return base-2 logarithm for power-of-two value.
 *
 * @param Value Input power-of-two.
 * @return Bit index.
 */
static U32 NtfsLog2(U32 Value) {
    U32 Shift = 0;

    while (Value > 1) {
        Value >>= 1;
        Shift++;
    }

    return Shift;
}

/***************************************************************************/

/**
 * @brief Read sectors from a mounted NTFS partition.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Sector Absolute disk sector.
 * @param NumSectors Number of sectors to read.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL NtfsReadSectors(
    LPNTFSFILESYSTEM FileSystem, SECTOR Sector, U32 NumSectors, LPVOID Buffer, U32 BufferSize) {
    IOCONTROL Control;
    U32 RelativeSector;
    U32 MaxBytes;
    U32 Result;

    if (FileSystem == NULL || Buffer == NULL) return FALSE;
    if (NumSectors == 0) return FALSE;

    if (Sector < FileSystem->PartitionStart) {
        WARNING(TEXT("[NtfsReadSectors] Sector underflow %u"), Sector);
        return FALSE;
    }

    RelativeSector = Sector - FileSystem->PartitionStart;
    if (RelativeSector >= FileSystem->PartitionSize) {
        WARNING(TEXT("[NtfsReadSectors] Sector out of partition %u"), Sector);
        return FALSE;
    }

    if (NumSectors > FileSystem->PartitionSize - RelativeSector) {
        WARNING(TEXT("[NtfsReadSectors] Read over partition boundary sector=%u count=%u"),
            Sector, NumSectors);
        return FALSE;
    }

    if (NumSectors > 0xFFFFFFFF / FileSystem->BytesPerSector) {
        WARNING(TEXT("[NtfsReadSectors] Byte size overflow count=%u"), NumSectors);
        return FALSE;
    }

    MaxBytes = NumSectors * FileSystem->BytesPerSector;
    if (BufferSize < MaxBytes) {
        WARNING(TEXT("[NtfsReadSectors] Buffer too small %u<%u"), BufferSize, MaxBytes);
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = NumSectors;
    Control.Buffer = Buffer;
    Control.BufferSize = MaxBytes;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("[NtfsReadSectors] Read failed result=%x"), Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode file record size from NTFS boot data.
 *
 * @param BootSector Boot sector structure.
 * @param BytesPerCluster Bytes per cluster.
 * @param RecordSizeOut Destination record size in bytes.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL NtfsComputeFileRecordSize(
    LPNTFS_MBR BootSector, U32 BytesPerCluster, U32* RecordSizeOut) {
    U8 RawValue;
    U32 RecordSize;

    if (RecordSizeOut != NULL) *RecordSizeOut = 0;
    if (BootSector == NULL || RecordSizeOut == NULL) return FALSE;

    RawValue = (U8)(BootSector->FileRecordSize & 0xFF);
    if (RawValue == 0) {
        WARNING(TEXT("[NtfsComputeFileRecordSize] Invalid file record size byte=0"));
        return FALSE;
    }

    if ((RawValue & 0x80) == 0) {
        RecordSize = ((U32)RawValue) * BytesPerCluster;
    } else {
        I8 SignedValue = (I8)RawValue;
        U8 Shift = (U8)(-SignedValue);

        if (Shift > 31) {
            WARNING(TEXT("[NtfsComputeFileRecordSize] Invalid file record exponent=%u"), Shift);
            return FALSE;
        }

        RecordSize = (U32)(1 << Shift);
    }

    if (RecordSize < NTFS_MIN_FILE_RECORD_SIZE || RecordSize > NTFS_MAX_FILE_RECORD_SIZE) {
        WARNING(TEXT("[NtfsComputeFileRecordSize] Unsupported file record size=%u"), RecordSize);
        return FALSE;
    }

    if (!NtfsIsPowerOfTwo(RecordSize)) {
        WARNING(TEXT("[NtfsComputeFileRecordSize] File record size not power-of-two=%u"), RecordSize);
        return FALSE;
    }

    *RecordSizeOut = RecordSize;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute absolute sector of $MFT record 0.
 *
 * @param PartitionStart Partition start sector.
 * @param SectorsPerCluster Cluster size in sectors.
 * @param MftStartCluster MFT start cluster.
 * @param SectorOut Destination absolute sector.
 * @return TRUE on success, FALSE on overflow or unsupported value.
 */
static BOOL NtfsComputeMftStartSector(
    SECTOR PartitionStart, U32 SectorsPerCluster, U64 MftStartCluster, U32* SectorOut) {
    U32 ClusterLow;
    U32 ClusterOffsetSectors;
    U32 MftSector;

    if (SectorOut != NULL) *SectorOut = 0;
    if (SectorOut == NULL) return FALSE;

    if (U64_High32(MftStartCluster) != 0) {
        WARNING(TEXT("[NtfsComputeMftStartSector] Unsupported MFT cluster high part=%x"),
            (U32)U64_High32(MftStartCluster));
        return FALSE;
    }

    ClusterLow = (U32)U64_Low32(MftStartCluster);
    if (ClusterLow > 0xFFFFFFFF / SectorsPerCluster) {
        WARNING(TEXT("[NtfsComputeMftStartSector] Cluster multiplication overflow cluster=%u"), ClusterLow);
        return FALSE;
    }

    ClusterOffsetSectors = ClusterLow * SectorsPerCluster;
    if (PartitionStart > 0xFFFFFFFF - ClusterOffsetSectors) {
        WARNING(TEXT("[NtfsComputeMftStartSector] Sector overflow start=%u"), PartitionStart);
        return FALSE;
    }

    MftSector = PartitionStart + ClusterOffsetSectors;
    *SectorOut = MftSector;

    return TRUE;
}

/***************************************************************************/
