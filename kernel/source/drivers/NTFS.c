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
#include "utils/Unicode.h"

/***************************************************************************/

#define NTFS_VER_MAJOR 1
#define NTFS_VER_MINOR 0
#define NTFS_MAX_SECTOR_SIZE 4096
#define NTFS_MIN_FILE_RECORD_SIZE 512
#define NTFS_MAX_FILE_RECORD_SIZE 4096
#define NTFS_ATTRIBUTE_STANDARD_INFORMATION 0x10
#define NTFS_ATTRIBUTE_FILE_NAME 0x30
#define NTFS_ATTRIBUTE_DATA 0x80
#define NTFS_ATTRIBUTE_INDEX_ROOT 0x90
#define NTFS_ATTRIBUTE_INDEX_ALLOCATION 0xA0
#define NTFS_ATTRIBUTE_BITMAP 0xB0
#define NTFS_ATTRIBUTE_END_MARKER 0xFFFFFFFF
#define NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE 0x18
#define NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE 0x40
#define NTFS_INDEX_ENTRY_FLAG_HAS_SUBNODE 0x0001
#define NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY 0x0002
#define NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE 66
#define NTFS_MAX_INDEX_ALLOCATION_BYTES (16 * N_1MB)
#define NTFS_FILE_NAME_NAMESPACE_POSIX 0
#define NTFS_FILE_NAME_NAMESPACE_WIN32 1
#define NTFS_FILE_NAME_NAMESPACE_DOS 2
#define NTFS_FILE_NAME_NAMESPACE_WIN32_DOS 3

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
    U32 FileRecordSize;
    U32 MftStartSector;
    U64 MftStartCluster;
    STR VolumeLabel[MAX_FS_LOGICAL_NAME];
} NTFSFILESYSTEM, *LPNTFSFILESYSTEM;

/***************************************************************************/

typedef struct tag_NTFS_FILE_RECORD_HEADER {
    U32 Magic;
    U16 UpdateSequenceOffset;
    U16 UpdateSequenceSize;
    U64 LogFileSequenceNumber;
    U16 SequenceNumber;
    U16 ReferenceCount;
    U16 SequenceOfAttributesOffset;
    U16 Flags;
    U32 RealSize;
    U32 AllocatedSize;
    U64 BaseRecord;
    U16 MaximumAttributeID;
    U16 Alignment;
    U32 RecordNumber;
} NTFS_FILE_RECORD_HEADER, *LPNTFS_FILE_RECORD_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_ROOT_HEADER {
    U32 AttributeType;
    U32 CollationRule;
    U32 IndexBlockSize;
    U8 ClustersPerIndexBlock;
    U8 Reserved[3];
} NTFS_INDEX_ROOT_HEADER, *LPNTFS_INDEX_ROOT_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_HEADER {
    U32 EntryOffset;
    U32 EntrySize;
    U32 AllocatedEntrySize;
    U8 Flags;
    U8 Reserved[3];
} NTFS_INDEX_HEADER, *LPNTFS_INDEX_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_RECORD_HEADER {
    U32 Magic;
    U16 UpdateSequenceOffset;
    U16 UpdateSequenceSize;
    U64 LogFileSequenceNumber;
    U64 Vcn;
    NTFS_INDEX_HEADER IndexHeader;
} NTFS_INDEX_RECORD_HEADER, *LPNTFS_INDEX_RECORD_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_FOLDER_ENUM_CONTEXT {
    LPNTFSFILESYSTEM FileSystem;
    LPNTFS_FOLDER_ENTRY_INFO Entries;
    U32 MaxEntries;
    U32 EntryCount;
    U32 TotalEntries;
    const U8* IndexAllocation;
    U32 IndexAllocationSize;
    U32 IndexBlockSize;
    const U8* Bitmap;
    U32 BitmapSize;
    U8* VisitedVcnMap;
    U32 VisitedVcnMapSize;
} NTFS_FOLDER_ENUM_CONTEXT, *LPNTFS_FOLDER_ENUM_CONTEXT;

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
 * @brief Load a U32 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
static U32 NtfsLoadU32(LPCVOID Address) {
    U32 Value;

    MemoryCopy(&Value, Address, sizeof(U32));
    return Value;
}

/***************************************************************************/

/**
 * @brief Load a U64 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
static U64 NtfsLoadU64(LPCVOID Address) {
    U64 Value;

    MemoryCopy(&Value, Address, sizeof(U64));
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
 * @brief Decode an unsigned little-endian integer from byte stream.
 *
 * @param Buffer Byte stream.
 * @param Size Number of bytes to decode.
 * @param ValueOut Decoded value.
 * @return TRUE on success, FALSE on invalid parameters.
 */
static BOOL NtfsLoadUnsignedLittleEndian(const U8* Buffer, U32 Size, U64* ValueOut) {
    U32 Low;
    U32 High;
    U32 Index;

    if (ValueOut != NULL) *ValueOut = U64_Make(0, 0);
    if (Buffer == NULL || ValueOut == NULL) return FALSE;
    if (Size == 0 || Size > 8) return FALSE;

    Low = 0;
    High = 0;
    for (Index = 0; Index < Size; Index++) {
        if (Index < 4) {
            Low |= ((U32)Buffer[Index]) << (Index * 8);
        } else {
            High |= ((U32)Buffer[Index]) << ((Index - 4) * 8);
        }
    }

    *ValueOut = U64_Make(High, Low);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode a signed little-endian integer from byte stream.
 *
 * @param Buffer Byte stream.
 * @param Size Number of bytes to decode.
 * @param ValueOut Decoded signed value.
 * @return TRUE on success, FALSE on invalid parameters.
 */
static BOOL NtfsLoadSignedLittleEndian(const U8* Buffer, U32 Size, I32* ValueOut) {
    U32 Value;
    U32 Index;

    if (ValueOut != NULL) *ValueOut = 0;
    if (Buffer == NULL || ValueOut == NULL) return FALSE;
    if (Size == 0 || Size > 4) return FALSE;

    Value = 0;
    for (Index = 0; Index < Size; Index++) {
        Value |= ((U32)Buffer[Index]) << (Index * 8);
    }

    if ((Buffer[Size - 1] & 0x80) != 0) {
        for (Index = Size; Index < 4; Index++) {
            Value |= ((U32)0xFF) << (Index * 8);
        }
    }

    *ValueOut = (I32)Value;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Rank FILE_NAME namespace priority.
 *
 * @param NameSpace NTFS FILE_NAME namespace value.
 * @return Rank where higher is preferred.
 */
static U32 NtfsGetFileNameNamespaceRank(U8 NameSpace) {
    switch (NameSpace) {
        case NTFS_FILE_NAME_NAMESPACE_WIN32:
        case NTFS_FILE_NAME_NAMESPACE_WIN32_DOS:
            return 4;
        case NTFS_FILE_NAME_NAMESPACE_POSIX:
            return 3;
        case NTFS_FILE_NAME_NAMESPACE_DOS:
            return 1;
        default:
            return 0;
    }
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

/**
 * @brief Apply NTFS update sequence fixup on a file record buffer.
 *
 * @param RecordBuffer Record buffer to patch in place.
 * @param RecordSize Record size in bytes.
 * @param SectorSize Sector size in bytes.
 * @param UpdateSequenceOffset Update sequence array offset.
 * @param UpdateSequenceSize Number of U16 values in update sequence array.
 * @return TRUE on success, FALSE on validation mismatch.
 */
static BOOL NtfsApplyFileRecordFixup(
    U8* RecordBuffer, U32 RecordSize, U32 SectorSize, U16 UpdateSequenceOffset, U16 UpdateSequenceSize) {
    U16 UpdateSequenceNumber;
    U32 SectorsInRecord;
    U32 FixupWords;
    U32 Index;

    if (RecordBuffer == NULL || SectorSize == 0) return FALSE;
    if (RecordSize == 0 || (RecordSize % SectorSize) != 0) return FALSE;
    if (UpdateSequenceSize < 2) return FALSE;

    SectorsInRecord = RecordSize / SectorSize;
    if ((U32)UpdateSequenceSize != (SectorsInRecord + 1)) {
        WARNING(TEXT("[NtfsApplyFileRecordFixup] Invalid update sequence size=%u sectors=%u"),
            UpdateSequenceSize, SectorsInRecord);
        return FALSE;
    }

    FixupWords = (U32)UpdateSequenceSize;
    if ((U32)UpdateSequenceOffset > RecordSize) return FALSE;
    if (FixupWords > (RecordSize - (U32)UpdateSequenceOffset) / sizeof(U16)) {
        WARNING(TEXT("[NtfsApplyFileRecordFixup] Update sequence out of range offset=%u words=%u"),
            UpdateSequenceOffset, FixupWords);
        return FALSE;
    }

    UpdateSequenceNumber = NtfsLoadU16(RecordBuffer + UpdateSequenceOffset);

    for (Index = 0; Index < SectorsInRecord; Index++) {
        U32 TailOffset = ((Index + 1) * SectorSize) - sizeof(U16);
        U16 TailValue = NtfsLoadU16(RecordBuffer + TailOffset);
        U16 Replacement;

        if (TailValue != UpdateSequenceNumber) {
            WARNING(TEXT("[NtfsApplyFileRecordFixup] Update sequence mismatch index=%u"), Index);
            return FALSE;
        }

        Replacement = NtfsLoadU16(
            RecordBuffer + UpdateSequenceOffset + ((Index + 1) * sizeof(U16)));
        NtfsStoreU16(RecordBuffer + TailOffset, Replacement);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Load one MFT file record into a dedicated contiguous buffer.
 *
 * The returned buffer has exactly FileRecordSize bytes and must be released
 * with KernelHeapFree by the caller.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT file record index.
 * @param RecordBufferOut Output pointer to allocated file record buffer.
 * @param HeaderOut Parsed and validated file record header.
 * @return TRUE on success, FALSE on read or validation failure.
 */
static BOOL NtfsLoadFileRecordBuffer(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8** RecordBufferOut, NTFS_FILE_RECORD_HEADER* HeaderOut) {
    U64 RecordOffset;
    U64 SectorOffset64;
    U32 SectorShift;
    U32 SectorOffset;
    U32 OffsetInSector;
    U32 TotalBytes;
    U32 NumSectors;
    U32 ReadSize;
    U32 RecordSector;
    U8* ReadBuffer;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;

    if (RecordBufferOut != NULL) *RecordBufferOut = NULL;
    if (HeaderOut != NULL) MemorySet(HeaderOut, 0, sizeof(NTFS_FILE_RECORD_HEADER));
    if (FileSystem == NULL || RecordBufferOut == NULL || HeaderOut == NULL) return FALSE;

    if (FileSystem->FileRecordSize == 0 || FileSystem->BytesPerSector == 0 ||
        !NtfsIsPowerOfTwo(FileSystem->BytesPerSector)) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Invalid NTFS geometry"));
        return FALSE;
    }

    RecordOffset = NtfsMultiplyU32ToU64(Index, FileSystem->FileRecordSize);
    SectorShift = NtfsLog2(FileSystem->BytesPerSector);
    SectorOffset64 = NtfsU64ShiftRight(RecordOffset, SectorShift);
    OffsetInSector = U64_Low32(RecordOffset) & (FileSystem->BytesPerSector - 1);

    if (U64_High32(SectorOffset64) != 0) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Sector offset too large index=%u"), Index);
        return FALSE;
    }
    SectorOffset = (U32)U64_Low32(SectorOffset64);

    if (FileSystem->MftStartSector > 0xFFFFFFFF - SectorOffset) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Record sector overflow index=%u"), Index);
        return FALSE;
    }
    RecordSector = FileSystem->MftStartSector + SectorOffset;

    if (OffsetInSector > 0xFFFFFFFF - FileSystem->FileRecordSize) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Record size overflow index=%u"), Index);
        return FALSE;
    }
    TotalBytes = OffsetInSector + FileSystem->FileRecordSize;
    NumSectors = TotalBytes / FileSystem->BytesPerSector;
    if ((TotalBytes % FileSystem->BytesPerSector) != 0) {
        NumSectors++;
    }

    if (NumSectors == 0 || NumSectors > 0xFFFFFFFF / FileSystem->BytesPerSector) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Invalid sector count index=%u"), Index);
        return FALSE;
    }
    ReadSize = NumSectors * FileSystem->BytesPerSector;

    ReadBuffer = (U8*)KernelHeapAlloc(ReadSize);
    if (ReadBuffer == NULL) {
        ERROR(TEXT("[NtfsLoadFileRecordBuffer] Unable to allocate %u bytes"), ReadSize);
        return FALSE;
    }

    if (!NtfsReadSectors(FileSystem, RecordSector, NumSectors, ReadBuffer, ReadSize)) {
        KernelHeapFree(ReadBuffer);
        return FALSE;
    }

    RecordBuffer = (U8*)KernelHeapAlloc(FileSystem->FileRecordSize);
    if (RecordBuffer == NULL) {
        ERROR(TEXT("[NtfsLoadFileRecordBuffer] Unable to allocate %u bytes"), FileSystem->FileRecordSize);
        KernelHeapFree(ReadBuffer);
        return FALSE;
    }

    MemoryCopy(RecordBuffer, ReadBuffer + OffsetInSector, FileSystem->FileRecordSize);
    KernelHeapFree(ReadBuffer);

    if (FileSystem->FileRecordSize < sizeof(NTFS_FILE_RECORD_HEADER)) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Record size too small=%u"), FileSystem->FileRecordSize);
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    MemoryCopy(&Header, RecordBuffer, sizeof(NTFS_FILE_RECORD_HEADER));
    if (Header.Magic != NTFS_FILE_RECORD_MAGIC) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Invalid file record magic=%x index=%u"), Header.Magic, Index);
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    if (!NtfsApplyFileRecordFixup(
            RecordBuffer,
            FileSystem->FileRecordSize,
            FileSystem->BytesPerSector,
            Header.UpdateSequenceOffset,
            Header.UpdateSequenceSize)) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Fixup failed index=%u"), Index);
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    MemoryCopy(&Header, RecordBuffer, sizeof(NTFS_FILE_RECORD_HEADER));
    if (Header.RealSize > FileSystem->FileRecordSize) {
        WARNING(TEXT("[NtfsLoadFileRecordBuffer] Invalid real size=%u index=%u"), Header.RealSize, Index);
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    *RecordBufferOut = RecordBuffer;
    MemoryCopy(HeaderOut, &Header, sizeof(NTFS_FILE_RECORD_HEADER));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parse a FILE_NAME attribute payload and update primary name metadata.
 *
 * @param FileNameValue FILE_NAME attribute value buffer.
 * @param FileNameLength FILE_NAME attribute value length.
 * @param RecordInfo Target record information to update.
 */
static void NtfsParseFileNameValue(
    const U8* FileNameValue, U32 FileNameLength, LPNTFS_FILE_RECORD_INFO RecordInfo) {
    U8 NameLength;
    U8 NameSpace;
    U32 CandidateRank;
    U32 CurrentRank;
    U32 Utf16Bytes;
    UINT Utf8Length;

    if (FileNameValue == NULL || RecordInfo == NULL) return;
    if (FileNameLength < 66) return;

    NameLength = FileNameValue[64];
    NameSpace = FileNameValue[65];
    Utf16Bytes = ((U32)NameLength) * sizeof(U16);

    if (66 > FileNameLength || Utf16Bytes > (FileNameLength - 66)) return;

    CandidateRank = NtfsGetFileNameNamespaceRank(NameSpace);
    CurrentRank = RecordInfo->HasPrimaryFileName ? NtfsGetFileNameNamespaceRank((U8)RecordInfo->PrimaryFileNameNamespace) : 0;
    if (RecordInfo->HasPrimaryFileName && CandidateRank < CurrentRank) return;

    StringClear(RecordInfo->PrimaryFileName);
    Utf8Length = 0;
    if (!Utf16LeToUtf8(
            (LPCUSTR)(FileNameValue + 66),
            NameLength,
            RecordInfo->PrimaryFileName,
            sizeof(RecordInfo->PrimaryFileName),
            &Utf8Length)) {
        return;
    }

    RecordInfo->HasPrimaryFileName = TRUE;
    RecordInfo->PrimaryFileNameNamespace = NameSpace;
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 8), &(RecordInfo->CreationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 16), &(RecordInfo->LastModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 24), &(RecordInfo->FileRecordModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 32), &(RecordInfo->LastAccessTime));
}

/***************************************************************************/

/**
 * @brief Parse FILE_NAME and DATA attributes from one file record.
 *
 * @param RecordBuffer File record buffer after fixup.
 * @param RecordSize File record size in bytes.
 * @param RecordInfo Parsed record metadata.
 * @param DataAttributeOffsetOut Output byte offset of selected DATA attribute.
 * @param DataAttributeLengthOut Output length of selected DATA attribute.
 * @return TRUE on success, FALSE on malformed attribute stream.
 */
static BOOL NtfsParseFileRecordAttributes(
    const U8* RecordBuffer,
    U32 RecordSize,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    U32* DataAttributeOffsetOut,
    U32* DataAttributeLengthOut) {
    U32 AttributeOffset;
    U32 AttributeType;
    U32 AttributeLength;
    BOOL IsNonResident;
    U8 NameLength;
    U32 ValueLength;
    U32 ValueOffset;
    U32 DataOffset;
    U32 DataLength;
    BOOL DataFound;

    if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = 0;
    if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = 0;
    if (RecordBuffer == NULL || RecordInfo == NULL) return FALSE;

    AttributeOffset = RecordInfo->SequenceOfAttributesOffset;
    if (AttributeOffset >= RecordInfo->UsedSize || AttributeOffset >= RecordSize) {
        WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid attribute offset=%u"), AttributeOffset);
        return FALSE;
    }

    DataFound = FALSE;
    while (AttributeOffset + 8 <= RecordInfo->UsedSize && AttributeOffset + 8 <= RecordSize) {
        AttributeType = NtfsLoadU32(RecordBuffer + AttributeOffset);
        if (AttributeType == NTFS_ATTRIBUTE_END_MARKER) return TRUE;

        AttributeLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 4);
        if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
            WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid attribute length=%u"), AttributeLength);
            return FALSE;
        }

        if (AttributeOffset > RecordInfo->UsedSize - AttributeLength ||
            AttributeOffset > RecordSize - AttributeLength) {
            WARNING(TEXT("[NtfsParseFileRecordAttributes] Attribute out of bounds offset=%u length=%u"),
                AttributeOffset, AttributeLength);
            return FALSE;
        }

        IsNonResident = RecordBuffer[AttributeOffset + 8] != 0;
        NameLength = RecordBuffer[AttributeOffset + 9];

        if (IsNonResident) {
            U16 RunListOffset;

            if (AttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) {
                WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid non-resident length=%u"), AttributeLength);
                return FALSE;
            }

            if (AttributeType == NTFS_ATTRIBUTE_DATA && !DataFound && NameLength == 0) {
                RecordInfo->HasDataAttribute = TRUE;
                RecordInfo->DataIsResident = FALSE;
                RecordInfo->AllocatedDataSize = NtfsLoadU64(RecordBuffer + AttributeOffset + 40);
                RecordInfo->DataSize = NtfsLoadU64(RecordBuffer + AttributeOffset + 48);
                RecordInfo->InitializedDataSize = NtfsLoadU64(RecordBuffer + AttributeOffset + 56);
                DataFound = TRUE;

                RunListOffset = NtfsLoadU16(RecordBuffer + AttributeOffset + 32);
                if (RunListOffset >= AttributeLength) {
                    WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid runlist offset=%u"), RunListOffset);
                    return FALSE;
                }

                DataOffset = AttributeOffset;
                DataLength = AttributeLength;
                if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = DataOffset;
                if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = DataLength;
            }
        } else {
            if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
                WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid resident length=%u"), AttributeLength);
                return FALSE;
            }

            ValueLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 16);
            ValueOffset = NtfsLoadU16(RecordBuffer + AttributeOffset + 20);
            if (ValueOffset > AttributeLength || ValueLength > (AttributeLength - ValueOffset)) {
                WARNING(TEXT("[NtfsParseFileRecordAttributes] Invalid resident value offset=%u length=%u"),
                    ValueOffset, ValueLength);
                return FALSE;
            }

            if (AttributeType == NTFS_ATTRIBUTE_FILE_NAME) {
                NtfsParseFileNameValue(
                    RecordBuffer + AttributeOffset + ValueOffset,
                    ValueLength,
                    RecordInfo);
            } else if (AttributeType == NTFS_ATTRIBUTE_DATA && !DataFound && NameLength == 0) {
                RecordInfo->HasDataAttribute = TRUE;
                RecordInfo->DataIsResident = TRUE;
                RecordInfo->DataSize = U64_FromU32(ValueLength);
                RecordInfo->AllocatedDataSize = U64_FromU32(ValueLength);
                RecordInfo->InitializedDataSize = U64_FromU32(ValueLength);
                DataFound = TRUE;

                DataOffset = AttributeOffset;
                DataLength = AttributeLength;
                if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = DataOffset;
                if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = DataLength;
            }
        }

        AttributeOffset += AttributeLength;
    }

    WARNING(TEXT("[NtfsParseFileRecordAttributes] Missing attribute end marker"));
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read one non-resident DATA stream using runlist mapping.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param DataAttribute Pointer to DATA attribute header.
 * @param DataAttributeLength DATA attribute length.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param DataSize Logical data size in bytes.
 * @param BytesReadOut Output number of bytes copied in Buffer.
 * @return TRUE on success, FALSE on malformed runlist or read failure.
 */
static BOOL NtfsReadNonResidentDataAttribute(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut) {
    U16 RunListOffset;
    const U8* RunPointer;
    const U8* RunEnd;
    U32 TargetBytes;
    U32 BytesWritten;
    I32 CurrentLcn;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || DataAttribute == NULL || Buffer == NULL) return FALSE;
    if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) return FALSE;

    TargetBytes = BufferSize;
    if (U64_High32(DataSize) == 0 && U64_Low32(DataSize) < TargetBytes) {
        TargetBytes = U64_Low32(DataSize);
    }

    if (TargetBytes == 0) {
        if (BytesReadOut != NULL) *BytesReadOut = 0;
        return TRUE;
    }

    RunListOffset = NtfsLoadU16(DataAttribute + 32);
    if (RunListOffset >= DataAttributeLength) {
        WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Invalid runlist offset=%u"), RunListOffset);
        return FALSE;
    }

    RunPointer = DataAttribute + RunListOffset;
    RunEnd = DataAttribute + DataAttributeLength;
    BytesWritten = 0;
    CurrentLcn = 0;

    while (RunPointer < RunEnd && BytesWritten < TargetBytes) {
        U8 Header;
        U32 LengthSize;
        U32 OffsetSize;
        U64 ClusterCount64;
        U32 ClusterCount;
        I32 LcnDelta;
        BOOL IsSparse;
        U32 RunBytes;
        U32 CopyBytes;

        Header = *RunPointer++;
        if (Header == 0) break;

        LengthSize = Header & 0x0F;
        OffsetSize = (Header >> 4) & 0x0F;
        if (LengthSize == 0) {
            WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Invalid run length size=0"));
            return FALSE;
        }

        if (RunPointer > RunEnd || LengthSize > (U32)(RunEnd - RunPointer) ||
            OffsetSize > (U32)(RunEnd - (RunPointer + LengthSize))) {
            WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Truncated runlist"));
            return FALSE;
        }

        if (!NtfsLoadUnsignedLittleEndian(RunPointer, LengthSize, &ClusterCount64)) return FALSE;
        RunPointer += LengthSize;

        if (U64_High32(ClusterCount64) != 0) {
            WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Cluster count too large"));
            return FALSE;
        }
        ClusterCount = U64_Low32(ClusterCount64);
        if (ClusterCount == 0) continue;

        IsSparse = OffsetSize == 0;
        LcnDelta = 0;
        if (!IsSparse) {
            if (!NtfsLoadSignedLittleEndian(RunPointer, OffsetSize, &LcnDelta)) return FALSE;
            CurrentLcn += LcnDelta;
        }
        RunPointer += OffsetSize;

        if (ClusterCount > 0xFFFFFFFF / FileSystem->BytesPerCluster) {
            WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Run byte size overflow"));
            return FALSE;
        }
        RunBytes = ClusterCount * FileSystem->BytesPerCluster;
        CopyBytes = TargetBytes - BytesWritten;
        if (CopyBytes > RunBytes) CopyBytes = RunBytes;

        if (IsSparse) {
            MemorySet((U8*)Buffer + BytesWritten, 0, CopyBytes);
        } else {
            U32 ClusterLcn;
            U32 SectorOffset;
            U32 StartSector;
            U32 ReadBytesAligned;
            U32 NumSectors;
            U8* ReadBuffer;

            if (CurrentLcn < 0) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Invalid LCN"));
                return FALSE;
            }

            ClusterLcn = (U32)CurrentLcn;
            if (ClusterLcn > 0xFFFFFFFF / FileSystem->SectorsPerCluster) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] LCN sector overflow"));
                return FALSE;
            }

            SectorOffset = ClusterLcn * FileSystem->SectorsPerCluster;
            if (FileSystem->PartitionStart > 0xFFFFFFFF - SectorOffset) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Partition sector overflow"));
                return FALSE;
            }
            StartSector = FileSystem->PartitionStart + SectorOffset;

            ReadBytesAligned = CopyBytes;
            if ((ReadBytesAligned % FileSystem->BytesPerSector) != 0) {
                ReadBytesAligned += FileSystem->BytesPerSector - (ReadBytesAligned % FileSystem->BytesPerSector);
            }
            NumSectors = ReadBytesAligned / FileSystem->BytesPerSector;

            ReadBuffer = (U8*)KernelHeapAlloc(ReadBytesAligned);
            if (ReadBuffer == NULL) {
                ERROR(TEXT("[NtfsReadNonResidentDataAttribute] Unable to allocate %u bytes"), ReadBytesAligned);
                return FALSE;
            }

            if (!NtfsReadSectors(FileSystem, StartSector, NumSectors, ReadBuffer, ReadBytesAligned)) {
                KernelHeapFree(ReadBuffer);
                return FALSE;
            }

            MemoryCopy((U8*)Buffer + BytesWritten, ReadBuffer, CopyBytes);
            KernelHeapFree(ReadBuffer);
        }

        BytesWritten += CopyBytes;
    }

    if (BytesReadOut != NULL) *BytesReadOut = BytesWritten;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether an NTFS attribute name matches "$I30".
 *
 * @param Attribute Attribute header pointer.
 * @param AttributeLength Total attribute length in bytes.
 * @return TRUE when unnamed or "$I30", FALSE otherwise.
 */
static BOOL NtfsIsI30AttributeName(const U8* Attribute, U32 AttributeLength) {
    static const USTR NtfsI30Name[4] = {'$', 'I', '3', '0'};
    U8 NameLength;
    U16 NameOffset;

    if (Attribute == NULL || AttributeLength < 16) return FALSE;

    NameLength = Attribute[9];
    if (NameLength == 0) return TRUE;

    NameOffset = NtfsLoadU16(Attribute + 10);
    if (NameOffset > AttributeLength) return FALSE;
    if (((U32)NameLength) > (AttributeLength - NameOffset) / sizeof(U16)) return FALSE;

    return Utf16LeCompareCaseInsensitiveAscii(
        (LPCUSTR)(Attribute + NameOffset),
        NameLength,
        NtfsI30Name,
        ARRAY_COUNT(NtfsI30Name));
}

/***************************************************************************/

/**
 * @brief Read full payload of one NTFS attribute.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Attribute Attribute header pointer.
 * @param AttributeLength Total attribute length in bytes.
 * @param ValueBufferOut Output allocated buffer with attribute payload.
 * @param ValueSizeOut Output payload size in bytes.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
static BOOL NtfsReadAttributeValue(
    LPNTFSFILESYSTEM FileSystem,
    const U8* Attribute,
    U32 AttributeLength,
    U8** ValueBufferOut,
    U32* ValueSizeOut) {
    BOOL IsNonResident;

    if (ValueBufferOut != NULL) *ValueBufferOut = NULL;
    if (ValueSizeOut != NULL) *ValueSizeOut = 0;
    if (FileSystem == NULL || Attribute == NULL || ValueBufferOut == NULL || ValueSizeOut == NULL) return FALSE;
    if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) return FALSE;

    IsNonResident = Attribute[8] != 0;
    if (!IsNonResident) {
        U32 ValueLength;
        U16 ValueOffset;
        U8* ValueBuffer;

        ValueLength = NtfsLoadU32(Attribute + 16);
        ValueOffset = NtfsLoadU16(Attribute + 20);
        if (ValueOffset > AttributeLength || ValueLength > (AttributeLength - ValueOffset)) return FALSE;

        if (ValueLength == 0) {
            *ValueBufferOut = NULL;
            *ValueSizeOut = 0;
            return TRUE;
        }

        ValueBuffer = (U8*)KernelHeapAlloc(ValueLength);
        if (ValueBuffer == NULL) {
            ERROR(TEXT("[NtfsReadAttributeValue] Unable to allocate %u bytes"), ValueLength);
            return FALSE;
        }

        MemoryCopy(ValueBuffer, Attribute + ValueOffset, ValueLength);
        *ValueBufferOut = ValueBuffer;
        *ValueSizeOut = ValueLength;
        return TRUE;
    }

    if (AttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) return FALSE;

    {
        U64 DataSize64 = NtfsLoadU64(Attribute + 48);
        U32 DataSize;
        U8* ValueBuffer;
        U32 BytesRead;

        if (U64_High32(DataSize64) != 0) {
            WARNING(TEXT("[NtfsReadAttributeValue] Attribute data size too large"));
            return FALSE;
        }

        DataSize = U64_Low32(DataSize64);
        if (DataSize > NTFS_MAX_INDEX_ALLOCATION_BYTES) {
            WARNING(TEXT("[NtfsReadAttributeValue] Attribute data size unsupported=%u"), DataSize);
            return FALSE;
        }

        if (DataSize == 0) {
            *ValueBufferOut = NULL;
            *ValueSizeOut = 0;
            return TRUE;
        }

        ValueBuffer = (U8*)KernelHeapAlloc(DataSize);
        if (ValueBuffer == NULL) {
            ERROR(TEXT("[NtfsReadAttributeValue] Unable to allocate %u bytes"), DataSize);
            return FALSE;
        }

        if (!NtfsReadNonResidentDataAttribute(
                FileSystem,
                Attribute,
                AttributeLength,
                ValueBuffer,
                DataSize,
                DataSize64,
                &BytesRead)) {
            KernelHeapFree(ValueBuffer);
            return FALSE;
        }

        if (BytesRead < DataSize) {
            MemorySet(ValueBuffer + BytesRead, 0, DataSize - BytesRead);
        }

        *ValueBufferOut = ValueBuffer;
        *ValueSizeOut = DataSize;
        return TRUE;
    }
}

/***************************************************************************/

/**
 * @brief Parse folder index-related attributes from a folder file record.
 *
 * @param RecordBuffer File record buffer.
 * @param RecordInfo Parsed file record info.
 * @param IndexRootOut Output pointer to INDEX_ROOT attribute.
 * @param IndexRootLengthOut Output length of INDEX_ROOT attribute.
 * @param IndexAllocationOut Output pointer to INDEX_ALLOCATION attribute.
 * @param IndexAllocationLengthOut Output length of INDEX_ALLOCATION attribute.
 * @param BitmapOut Output pointer to BITMAP attribute.
 * @param BitmapLengthOut Output length of BITMAP attribute.
 * @return TRUE on success, FALSE on malformed attributes.
 */
static BOOL NtfsParseFolderIndexAttributes(
    const U8* RecordBuffer,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    const U8** IndexRootOut,
    U32* IndexRootLengthOut,
    const U8** IndexAllocationOut,
    U32* IndexAllocationLengthOut,
    const U8** BitmapOut,
    U32* BitmapLengthOut) {
    U32 AttributeOffset;

    if (IndexRootOut != NULL) *IndexRootOut = NULL;
    if (IndexRootLengthOut != NULL) *IndexRootLengthOut = 0;
    if (IndexAllocationOut != NULL) *IndexAllocationOut = NULL;
    if (IndexAllocationLengthOut != NULL) *IndexAllocationLengthOut = 0;
    if (BitmapOut != NULL) *BitmapOut = NULL;
    if (BitmapLengthOut != NULL) *BitmapLengthOut = 0;

    if (RecordBuffer == NULL || RecordInfo == NULL) return FALSE;

    AttributeOffset = RecordInfo->SequenceOfAttributesOffset;
    while (AttributeOffset + 8 <= RecordInfo->UsedSize) {
        U32 AttributeType = NtfsLoadU32(RecordBuffer + AttributeOffset);
        U32 AttributeLength;
        const U8* Attribute;

        if (AttributeType == NTFS_ATTRIBUTE_END_MARKER) return TRUE;

        AttributeLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 4);
        if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) return FALSE;
        if (AttributeOffset > RecordInfo->UsedSize - AttributeLength) return FALSE;

        Attribute = RecordBuffer + AttributeOffset;
        if (AttributeType == NTFS_ATTRIBUTE_INDEX_ROOT) {
            if (Attribute[8] != 0 || !NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (IndexRootOut != NULL) *IndexRootOut = Attribute;
            if (IndexRootLengthOut != NULL) *IndexRootLengthOut = AttributeLength;
        } else if (AttributeType == NTFS_ATTRIBUTE_INDEX_ALLOCATION) {
            if (Attribute[8] == 0 || !NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (IndexAllocationOut != NULL) *IndexAllocationOut = Attribute;
            if (IndexAllocationLengthOut != NULL) *IndexAllocationLengthOut = AttributeLength;
        } else if (AttributeType == NTFS_ATTRIBUTE_BITMAP) {
            if (!NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (BitmapOut != NULL) *BitmapOut = Attribute;
            if (BitmapLengthOut != NULL) *BitmapLengthOut = AttributeLength;
        }

        AttributeOffset += AttributeLength;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode MFT record index from index-entry file reference.
 *
 * @param FileReference Pointer to 8-byte file reference field.
 * @param RecordIndexOut Output decoded record index.
 * @return TRUE on success, FALSE when index exceeds 32-bit range.
 */
static BOOL NtfsDecodeIndexEntryRecordIndex(const U8* FileReference, U32* RecordIndexOut) {
    if (RecordIndexOut != NULL) *RecordIndexOut = 0;
    if (FileReference == NULL || RecordIndexOut == NULL) return FALSE;

    if (FileReference[4] != 0 || FileReference[5] != 0) {
        WARNING(TEXT("[NtfsDecodeIndexEntryRecordIndex] Record index out of range"));
        return FALSE;
    }

    *RecordIndexOut = NtfsLoadU32(FileReference);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode FILE_NAME payload into folder entry information.
 *
 * @param FileNameValue FILE_NAME payload pointer.
 * @param FileNameLength FILE_NAME payload length in bytes.
 * @param EntryInfo Output folder entry information.
 * @return TRUE on success, FALSE on malformed value.
 */
static BOOL NtfsDecodeFolderEntryFileName(
    const U8* FileNameValue, U32 FileNameLength, LPNTFS_FOLDER_ENTRY_INFO EntryInfo) {
    U8 NameLength;
    U32 Utf16Bytes;
    UINT Utf8Length;

    if (EntryInfo != NULL) MemorySet(EntryInfo, 0, sizeof(NTFS_FOLDER_ENTRY_INFO));
    if (FileNameValue == NULL || EntryInfo == NULL) return FALSE;
    if (FileNameLength < NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE) return FALSE;

    NameLength = FileNameValue[64];
    EntryInfo->NameSpace = FileNameValue[65];
    Utf16Bytes = ((U32)NameLength) * sizeof(U16);
    if (Utf16Bytes > (FileNameLength - NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE)) return FALSE;

    if (!Utf16LeToUtf8(
            (LPCUSTR)(FileNameValue + NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE),
            NameLength,
            EntryInfo->Name,
            sizeof(EntryInfo->Name),
            &Utf8Length)) {
        return FALSE;
    }

    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 8), &(EntryInfo->CreationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 16), &(EntryInfo->LastModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 24), &(EntryInfo->FileRecordModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 32), &(EntryInfo->LastAccessTime));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return TRUE when one folder entry is already present in output list.
 *
 * @param Context Enumeration context.
 * @param Entry Candidate entry.
 * @return TRUE if duplicate entry exists, FALSE otherwise.
 */
static BOOL NtfsFolderEntryAlreadyPresent(
    LPNTFS_FOLDER_ENUM_CONTEXT Context, LPNTFS_FOLDER_ENTRY_INFO Entry) {
    U32 Index;

    if (Context == NULL || Entry == NULL) return FALSE;
    if (Context->Entries == NULL) return FALSE;

    for (Index = 0; Index < Context->EntryCount; Index++) {
        LPNTFS_FOLDER_ENTRY_INFO Current = Context->Entries + Index;
        if (Current->FileRecordIndex == Entry->FileRecordIndex &&
            StringCompare(Current->Name, Entry->Name) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Add one folder entry decoded from index key data.
 *
 * @param Context Enumeration context.
 * @param EntryBuffer Pointer to index entry buffer.
 * @param EntryLength Index entry length.
 * @param KeyLength Index key length.
 * @return TRUE on success, FALSE on malformed key data.
 */
static BOOL NtfsAddFolderEntryFromIndexKey(
    LPNTFS_FOLDER_ENUM_CONTEXT Context, const U8* EntryBuffer, U32 EntryLength, U32 KeyLength) {
    NTFS_FOLDER_ENTRY_INFO EntryInfo;
    U32 FileRecordIndex;
    NTFS_FILE_RECORD_INFO RecordInfo;

    if (Context == NULL || EntryBuffer == NULL) return FALSE;
    if (EntryLength < 16 || KeyLength > (EntryLength - 16)) return FALSE;
    if (KeyLength < NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE) return TRUE;

    if (!NtfsDecodeIndexEntryRecordIndex(EntryBuffer, &FileRecordIndex)) return TRUE;
    if (!NtfsDecodeFolderEntryFileName(EntryBuffer + 16, KeyLength, &EntryInfo)) return TRUE;

    if (StringCompare(EntryInfo.Name, TEXT(".")) == 0 || StringCompare(EntryInfo.Name, TEXT("..")) == 0) {
        return TRUE;
    }

    EntryInfo.FileRecordIndex = FileRecordIndex;
    MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (NtfsReadFileRecord((LPFILESYSTEM)Context->FileSystem, FileRecordIndex, &RecordInfo)) {
        EntryInfo.IsFolder = (RecordInfo.Flags & NTFS_FR_FLAG_FOLDER) != 0;
    }

    if (NtfsFolderEntryAlreadyPresent(Context, &EntryInfo)) return TRUE;

    Context->TotalEntries++;
    if (Context->Entries != NULL && Context->EntryCount < Context->MaxEntries) {
        MemoryCopy(Context->Entries + Context->EntryCount, &EntryInfo, sizeof(NTFS_FOLDER_ENTRY_INFO));
        Context->EntryCount++;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether one index-allocation VCN is marked used in bitmap.
 *
 * @param Context Enumeration context.
 * @param Vcn Index-allocation VCN.
 * @return TRUE when the VCN is used or no bitmap is available.
 */
static BOOL NtfsIsIndexAllocationVcnUsed(LPNTFS_FOLDER_ENUM_CONTEXT Context, U32 Vcn) {
    U32 ByteIndex;
    U8 BitMask;

    if (Context == NULL) return FALSE;
    if (Context->Bitmap == NULL || Context->BitmapSize == 0) return TRUE;

    ByteIndex = Vcn / 8;
    if (ByteIndex >= Context->BitmapSize) return FALSE;

    BitMask = (U8)(1 << (Vcn % 8));
    return (Context->Bitmap[ByteIndex] & BitMask) != 0;
}

/***************************************************************************/

/**
 * @brief Mark one index-allocation VCN as visited.
 *
 * @param Context Enumeration context.
 * @param Vcn VCN to mark.
 * @return TRUE when VCN was not visited before and is now marked.
 */
static BOOL NtfsMarkIndexAllocationVcnVisited(LPNTFS_FOLDER_ENUM_CONTEXT Context, U32 Vcn) {
    U32 ByteIndex;
    U8 BitMask;

    if (Context == NULL || Context->VisitedVcnMap == NULL) return FALSE;

    ByteIndex = Vcn / 8;
    if (ByteIndex >= Context->VisitedVcnMapSize) return FALSE;

    BitMask = (U8)(1 << (Vcn % 8));
    if ((Context->VisitedVcnMap[ByteIndex] & BitMask) != 0) return FALSE;

    Context->VisitedVcnMap[ByteIndex] |= BitMask;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Traverse one NTFS index-header entry array.
 *
 * @param Context Enumeration context.
 * @param Header Pointer to index header.
 * @param HeaderRegionSize Available bytes from Header to end of region.
 * @param PendingVcns VCN stack storage.
 * @param PendingCountInOut In/out number of pending VCN entries.
 * @param PendingCapacity Capacity of PendingVcns array.
 * @return TRUE on success, FALSE on malformed entry stream.
 */
static BOOL NtfsTraverseIndexHeader(
    LPNTFS_FOLDER_ENUM_CONTEXT Context,
    const NTFS_INDEX_HEADER* Header,
    U32 HeaderRegionSize,
    U32* PendingVcns,
    U32* PendingCountInOut,
    U32 PendingCapacity) {
    U32 EntryOffset;
    U32 EntrySize;
    U32 Cursor;

    if (Context == NULL || Header == NULL || PendingCountInOut == NULL) return FALSE;
    if (HeaderRegionSize < sizeof(NTFS_INDEX_HEADER)) return FALSE;

    EntryOffset = Header->EntryOffset;
    EntrySize = Header->EntrySize;
    if (EntryOffset > HeaderRegionSize || EntrySize > (HeaderRegionSize - EntryOffset)) {
        WARNING(TEXT("[NtfsTraverseIndexHeader] Invalid index header bounds"));
        return FALSE;
    }

    Cursor = 0;
    while (Cursor + 16 <= EntrySize) {
        const U8* Entry = ((const U8*)Header) + EntryOffset + Cursor;
        U16 Length = NtfsLoadU16(Entry + 8);
        U16 KeyLength = NtfsLoadU16(Entry + 10);
        U16 Flags = NtfsLoadU16(Entry + 12);

        if (Length < 16 || Length > (EntrySize - Cursor)) {
            WARNING(TEXT("[NtfsTraverseIndexHeader] Invalid index entry length=%u"), Length);
            return FALSE;
        }

        if ((Flags & NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY) == 0) {
            if (!NtfsAddFolderEntryFromIndexKey(Context, Entry, Length, KeyLength)) return FALSE;
        }

        if ((Flags & NTFS_INDEX_ENTRY_FLAG_HAS_SUBNODE) != 0) {
            U64 Vcn64;

            if (Length < 24) return FALSE;
            Vcn64 = NtfsLoadU64(Entry + Length - sizeof(U64));
            if (U64_High32(Vcn64) != 0) {
                WARNING(TEXT("[NtfsTraverseIndexHeader] Subnode VCN too large"));
                return FALSE;
            }

            if (PendingVcns != NULL && *PendingCountInOut < PendingCapacity) {
                PendingVcns[*PendingCountInOut] = U64_Low32(Vcn64);
                (*PendingCountInOut)++;
            }
        }

        Cursor += Length;
        if ((Flags & NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY) != 0) break;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply update-sequence fixup on all index-allocation records.
 *
 * @param Context Enumeration context.
 * @return TRUE on success, FALSE on malformed index record.
 */
static BOOL NtfsPrepareIndexAllocationRecords(LPNTFS_FOLDER_ENUM_CONTEXT Context) {
    U32 RecordCount;
    U32 Index;

    if (Context == NULL || Context->IndexAllocation == NULL || Context->IndexBlockSize == 0) return TRUE;
    if (Context->IndexAllocationSize == 0) return TRUE;
    if ((Context->IndexAllocationSize % Context->IndexBlockSize) != 0) return FALSE;

    RecordCount = Context->IndexAllocationSize / Context->IndexBlockSize;
    for (Index = 0; Index < RecordCount; Index++) {
        U8* Record;
        NTFS_INDEX_RECORD_HEADER Header;

        if (!NtfsIsIndexAllocationVcnUsed(Context, Index)) continue;

        Record = (U8*)Context->IndexAllocation + (Index * Context->IndexBlockSize);
        MemoryCopy(&Header, Record, sizeof(NTFS_INDEX_RECORD_HEADER));
        if (Header.Magic != 0x58444E49) continue;

        if (!NtfsApplyFileRecordFixup(
                Record,
                Context->IndexBlockSize,
                Context->FileSystem->BytesPerSector,
                Header.UpdateSequenceOffset,
                Header.UpdateSequenceSize)) {
            WARNING(TEXT("[NtfsPrepareIndexAllocationRecords] Fixup failed vcn=%u"), Index);
            return FALSE;
        }
    }

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
    U32 FileRecordSize;
    U32 MftStartSector;
    U64 MftStartCluster;
    SECTOR PartitionStart;
    NTFS_FILE_RECORD_INFO RecordInfo;

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
    if (!NtfsComputeFileRecordSize(BootSector, BytesPerCluster, &FileRecordSize)) {
        return FALSE;
    }

    if (!NtfsComputeMftStartSector(PartitionStart, SectorsPerCluster, MftStartCluster, &MftStartSector)) {
        return FALSE;
    }

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
    FileSystem->FileRecordSize = FileRecordSize;
    FileSystem->MftStartSector = MftStartSector;
    FileSystem->MftStartCluster = MftStartCluster;
    StringClear(FileSystem->VolumeLabel);

    ListAddItem(GetFileSystemList(), FileSystem);

    DEBUG(TEXT("[MountPartition_NTFS] Mounted %s bytes_per_sector=%u sectors_per_cluster=%u record_size=%u mft_cluster=%x%08x"),
        FileSystem->Header.Name,
        FileSystem->BytesPerSector,
        FileSystem->SectorsPerCluster,
        FileSystem->FileRecordSize,
        (U32)U64_High32(FileSystem->MftStartCluster),
        (U32)U64_Low32(FileSystem->MftStartCluster));

    MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (NtfsReadFileRecord((LPFILESYSTEM)FileSystem, 0, &RecordInfo)) {
        DEBUG(TEXT("[MountPartition_NTFS] MFT[0] flags=%x attrs=%u used=%u name=%s"),
            RecordInfo.Flags,
            RecordInfo.SequenceOfAttributesOffset,
            RecordInfo.UsedSize,
            RecordInfo.HasPrimaryFileName ? RecordInfo.PrimaryFileName : TEXT("<none>"));
    } else {
        WARNING(TEXT("[MountPartition_NTFS] MFT[0] read failed"));
    }

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
        Geometry->FileRecordSize = NtfsFileSystem->FileRecordSize;
        Geometry->MftStartCluster = NtfsFileSystem->MftStartCluster;
        StringCopy(Geometry->VolumeLabel, NtfsFileSystem->VolumeLabel);

        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read one MFT file record and parse the base record header.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param RecordInfo Destination metadata structure.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsReadFileRecord(LPFILESYSTEM FileSystem, U32 Index, LPNTFS_FILE_RECORD_INFO RecordInfo) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;

    if (RecordInfo != NULL) {
        MemorySet(RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    }

    if (FileSystem == NULL || RecordInfo == NULL) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        if (!NtfsLoadFileRecordBuffer(NtfsFileSystem, Index, &RecordBuffer, &Header)) {
            return FALSE;
        }

        RecordInfo->Index = Index;
        RecordInfo->RecordSize = NtfsFileSystem->FileRecordSize;
        RecordInfo->UsedSize = Header.RealSize;
        RecordInfo->Flags = Header.Flags;
        RecordInfo->SequenceNumber = Header.SequenceNumber;
        RecordInfo->ReferenceCount = Header.ReferenceCount;
        RecordInfo->SequenceOfAttributesOffset = Header.SequenceOfAttributesOffset;
        RecordInfo->UpdateSequenceOffset = Header.UpdateSequenceOffset;
        RecordInfo->UpdateSequenceSize = Header.UpdateSequenceSize;
        DataAttributeOffset = 0;
        DataAttributeLength = 0;

        if (!NtfsParseFileRecordAttributes(
                RecordBuffer,
                NtfsFileSystem->FileRecordSize,
                RecordInfo,
                &DataAttributeOffset,
                &DataAttributeLength)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        KernelHeapFree(RecordBuffer);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read default DATA stream for one file record by MFT index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for bytes copied to Buffer.
 * @return TRUE on success, FALSE on malformed attributes or read failure.
 */
BOOL NtfsReadFileDataByIndex(
    LPFILESYSTEM FileSystem, U32 Index, LPVOID Buffer, U32 BufferSize, U32* BytesReadOut) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;
    NTFS_FILE_RECORD_INFO RecordInfo;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || Buffer == NULL) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        U8* DataAttribute;
        U32 ValueLength;
        U32 ValueOffset;
        U32 BytesToCopy;

        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        if (!NtfsLoadFileRecordBuffer(NtfsFileSystem, Index, &RecordBuffer, &Header)) {
            return FALSE;
        }

        MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
        RecordInfo.Index = Index;
        RecordInfo.RecordSize = NtfsFileSystem->FileRecordSize;
        RecordInfo.UsedSize = Header.RealSize;
        RecordInfo.Flags = Header.Flags;
        RecordInfo.SequenceNumber = Header.SequenceNumber;
        RecordInfo.ReferenceCount = Header.ReferenceCount;
        RecordInfo.SequenceOfAttributesOffset = Header.SequenceOfAttributesOffset;
        RecordInfo.UpdateSequenceOffset = Header.UpdateSequenceOffset;
        RecordInfo.UpdateSequenceSize = Header.UpdateSequenceSize;

        DataAttributeOffset = 0;
        DataAttributeLength = 0;
        if (!NtfsParseFileRecordAttributes(
                RecordBuffer,
                NtfsFileSystem->FileRecordSize,
                &RecordInfo,
                &DataAttributeOffset,
                &DataAttributeLength)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        if (!RecordInfo.HasDataAttribute || DataAttributeLength == 0) {
            KernelHeapFree(RecordBuffer);
            if (BytesReadOut != NULL) *BytesReadOut = 0;
            return TRUE;
        }

        DataAttribute = RecordBuffer + DataAttributeOffset;
        if (RecordInfo.DataIsResident) {
            if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }

            ValueLength = NtfsLoadU32(DataAttribute + 16);
            ValueOffset = NtfsLoadU16(DataAttribute + 20);
            if (ValueOffset > DataAttributeLength || ValueLength > (DataAttributeLength - ValueOffset)) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }

            BytesToCopy = BufferSize;
            if (ValueLength < BytesToCopy) BytesToCopy = ValueLength;
            if (BytesToCopy > 0) {
                MemoryCopy(Buffer, DataAttribute + ValueOffset, BytesToCopy);
            }
            if (BytesReadOut != NULL) *BytesReadOut = BytesToCopy;
            KernelHeapFree(RecordBuffer);
            return TRUE;
        }

        if (!NtfsReadNonResidentDataAttribute(
                NtfsFileSystem,
                DataAttribute,
                DataAttributeLength,
                Buffer,
                BufferSize,
                RecordInfo.DataSize,
                BytesReadOut)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        KernelHeapFree(RecordBuffer);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Enumerate one NTFS folder by file-record index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param FolderIndex Folder file-record index.
 * @param Entries Optional output array for folder entries.
 * @param MaxEntries Capacity of Entries.
 * @param EntryCountOut Optional output number of stored entries.
 * @param TotalEntriesOut Optional output total number of enumerated entries.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
BOOL NtfsEnumerateFolderByIndex(
    LPFILESYSTEM FileSystem,
    U32 FolderIndex,
    LPNTFS_FOLDER_ENTRY_INFO Entries,
    U32 MaxEntries,
    U32* EntryCountOut,
    U32* TotalEntriesOut) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER RecordHeader;
    NTFS_FILE_RECORD_INFO RecordInfo;
    const U8* IndexRootAttribute;
    U32 IndexRootAttributeLength;
    const U8* IndexAllocationAttribute;
    U32 IndexAllocationAttributeLength;
    const U8* BitmapAttribute;
    U32 BitmapAttributeLength;
    U8* IndexRootValue;
    U32 IndexRootValueSize;
    U8* IndexAllocationData;
    U32 IndexAllocationDataSize;
    U8* BitmapData;
    U32 BitmapDataSize;
    NTFS_INDEX_ROOT_HEADER RootHeader;
    NTFS_FOLDER_ENUM_CONTEXT Context;
    U32 MaxVcnRecords;
    U32* PendingVcns;
    U32 PendingCount;
    BOOL Result;

    if (EntryCountOut != NULL) *EntryCountOut = 0;
    if (TotalEntriesOut != NULL) *TotalEntriesOut = 0;
    if (FileSystem == NULL) return FALSE;
    if (Entries == NULL && MaxEntries != 0) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;
        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;

        if (!NtfsLoadFileRecordBuffer(NtfsFileSystem, FolderIndex, &RecordBuffer, &RecordHeader)) {
            return FALSE;
        }

        MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
        RecordInfo.Index = FolderIndex;
        RecordInfo.RecordSize = NtfsFileSystem->FileRecordSize;
        RecordInfo.UsedSize = RecordHeader.RealSize;
        RecordInfo.Flags = RecordHeader.Flags;
        RecordInfo.SequenceNumber = RecordHeader.SequenceNumber;
        RecordInfo.ReferenceCount = RecordHeader.ReferenceCount;
        RecordInfo.SequenceOfAttributesOffset = RecordHeader.SequenceOfAttributesOffset;
        RecordInfo.UpdateSequenceOffset = RecordHeader.UpdateSequenceOffset;
        RecordInfo.UpdateSequenceSize = RecordHeader.UpdateSequenceSize;

        if ((RecordInfo.Flags & NTFS_FR_FLAG_FOLDER) == 0) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        IndexRootAttribute = NULL;
        IndexRootAttributeLength = 0;
        IndexAllocationAttribute = NULL;
        IndexAllocationAttributeLength = 0;
        BitmapAttribute = NULL;
        BitmapAttributeLength = 0;

        if (!NtfsParseFolderIndexAttributes(
                RecordBuffer,
                &RecordInfo,
                &IndexRootAttribute,
                &IndexRootAttributeLength,
                &IndexAllocationAttribute,
                &IndexAllocationAttributeLength,
                &BitmapAttribute,
                &BitmapAttributeLength)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        if (IndexRootAttribute == NULL || IndexRootAttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        if (!NtfsReadAttributeValue(
                NtfsFileSystem,
                IndexRootAttribute,
                IndexRootAttributeLength,
                &IndexRootValue,
                &IndexRootValueSize)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        if (IndexRootValue == NULL || IndexRootValueSize < sizeof(NTFS_INDEX_ROOT_HEADER) + sizeof(NTFS_INDEX_HEADER)) {
            KernelHeapFree(RecordBuffer);
            if (IndexRootValue != NULL) KernelHeapFree(IndexRootValue);
            return FALSE;
        }

        MemoryCopy(&RootHeader, IndexRootValue, sizeof(NTFS_INDEX_ROOT_HEADER));
        if (RootHeader.IndexBlockSize == 0 || !NtfsIsPowerOfTwo(RootHeader.IndexBlockSize)) {
            KernelHeapFree(RecordBuffer);
            KernelHeapFree(IndexRootValue);
            return FALSE;
        }

        IndexAllocationData = NULL;
        IndexAllocationDataSize = 0;
        if (IndexAllocationAttribute != NULL && IndexAllocationAttributeLength >= NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) {
            if (!NtfsReadAttributeValue(
                    NtfsFileSystem,
                    IndexAllocationAttribute,
                    IndexAllocationAttributeLength,
                    &IndexAllocationData,
                    &IndexAllocationDataSize)) {
                KernelHeapFree(RecordBuffer);
                KernelHeapFree(IndexRootValue);
                return FALSE;
            }
        }

        BitmapData = NULL;
        BitmapDataSize = 0;
        if (BitmapAttribute != NULL && BitmapAttributeLength >= NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
            if (!NtfsReadAttributeValue(
                    NtfsFileSystem,
                    BitmapAttribute,
                    BitmapAttributeLength,
                    &BitmapData,
                    &BitmapDataSize)) {
                KernelHeapFree(RecordBuffer);
                KernelHeapFree(IndexRootValue);
                if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
                return FALSE;
            }
        }

        MemorySet(&Context, 0, sizeof(NTFS_FOLDER_ENUM_CONTEXT));
        Context.FileSystem = NtfsFileSystem;
        Context.Entries = Entries;
        Context.MaxEntries = MaxEntries;
        Context.EntryCount = 0;
        Context.TotalEntries = 0;
        Context.IndexAllocation = IndexAllocationData;
        Context.IndexAllocationSize = IndexAllocationDataSize;
        Context.IndexBlockSize = RootHeader.IndexBlockSize;
        Context.Bitmap = BitmapData;
        Context.BitmapSize = BitmapDataSize;

        MaxVcnRecords = 0;
        if (Context.IndexAllocation != NULL && Context.IndexBlockSize != 0) {
            if ((Context.IndexAllocationSize % Context.IndexBlockSize) != 0) {
                KernelHeapFree(RecordBuffer);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
            MaxVcnRecords = Context.IndexAllocationSize / Context.IndexBlockSize;
        }

        Context.VisitedVcnMap = NULL;
        Context.VisitedVcnMapSize = 0;
        if (MaxVcnRecords > 0) {
            Context.VisitedVcnMapSize = (MaxVcnRecords + 7) / 8;
            Context.VisitedVcnMap = (U8*)KernelHeapAlloc(Context.VisitedVcnMapSize);
            if (Context.VisitedVcnMap == NULL) {
                KernelHeapFree(RecordBuffer);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
            MemorySet(Context.VisitedVcnMap, 0, Context.VisitedVcnMapSize);
        }

        if (!NtfsPrepareIndexAllocationRecords(&Context)) {
            if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
            KernelHeapFree(RecordBuffer);
            KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        PendingVcns = NULL;
        PendingCount = 0;
        if (MaxVcnRecords > 0) {
            PendingVcns = (U32*)KernelHeapAlloc(MaxVcnRecords * sizeof(U32));
            if (PendingVcns == NULL) {
                if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
                KernelHeapFree(RecordBuffer);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
        }

        Result = NtfsTraverseIndexHeader(
            &Context,
            (const NTFS_INDEX_HEADER*)(IndexRootValue + sizeof(NTFS_INDEX_ROOT_HEADER)),
            IndexRootValueSize - sizeof(NTFS_INDEX_ROOT_HEADER),
            PendingVcns,
            &PendingCount,
            MaxVcnRecords);

        while (Result && PendingCount > 0) {
            U32 Vcn;
            U32 RecordOffset;
            U8* RecordBufferNode;
            NTFS_INDEX_RECORD_HEADER NodeHeader;

            PendingCount--;
            Vcn = PendingVcns[PendingCount];

            if (!NtfsIsIndexAllocationVcnUsed(&Context, Vcn)) continue;
            if (!NtfsMarkIndexAllocationVcnVisited(&Context, Vcn)) continue;
            if (Vcn >= MaxVcnRecords) continue;

            RecordOffset = Vcn * Context.IndexBlockSize;
            RecordBufferNode = (U8*)Context.IndexAllocation + RecordOffset;

            MemoryCopy(&NodeHeader, RecordBufferNode, sizeof(NTFS_INDEX_RECORD_HEADER));
            if (NodeHeader.Magic != 0x58444E49) continue;

            Result = NtfsTraverseIndexHeader(
                &Context,
                (const NTFS_INDEX_HEADER*)(RecordBufferNode + 24),
                Context.IndexBlockSize - 24,
                PendingVcns,
                &PendingCount,
                MaxVcnRecords);
        }

        if (EntryCountOut != NULL) *EntryCountOut = Context.EntryCount;
        if (TotalEntriesOut != NULL) *TotalEntriesOut = Context.TotalEntries;

        if (PendingVcns != NULL) KernelHeapFree(PendingVcns);
        if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
        KernelHeapFree(RecordBuffer);
        KernelHeapFree(IndexRootValue);
        if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
        if (BitmapData != NULL) KernelHeapFree(BitmapData);

        return Result;
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
