
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


    NTFS

\************************************************************************/

#ifndef NTFS_H_INCLUDED
#define NTFS_H_INCLUDED

/***************************************************************************/

#include "FSID.h"
#include "FileSystem.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// The NTFS Master Boot Record
// Code begins at 0x005D

typedef struct tag_NTFS_MBR {
    U8 Jump[3];
    U8 OEMName[8];  // "NTFS"
    U16 BytesPerSector;
    U8 SectorsPerCluster;
    U8 Unused1[7];
    U8 MediaDescriptor;  // 0xF8 for Hard Disks
    U16 Unused2;
    U16 SectorsPerTrack;
    U16 NumHeads;  // Number of heads of media
    U8 Unused3[8];
    U16 Unknown1;  // 0x0080 ?
    U16 Unknown2;  // 0x0080 ?
    U64 SectorsInUnit;
    U64 LCN_VCN0_MFT;
    U64 LCN_VCN0_MFTMIRR;
    U32 FileRecordSize;   // In clusters
    U32 IndexBufferSize;  // In clusters
    U32 SerialNumber;
    U8 Unused4[13];
    U8 Code[417];
    U16 BIOSMark;  // 0xAA55
} NTFS_MBR, *LPNTFS_MBR;

/***************************************************************************/

typedef struct tag_NTFS_FILEREF {
    U32 Low;
    U32 High;
} NTFS_FILEREF, *LPNTFS_FILEREF;

/***************************************************************************/

#define NTFS_FR_END_MARK 0xFFFFFFFF
#define NTFS_FILE_RECORD_MAGIC 0x454C4946

#define NTFS_FR_FLAG_IN_USE 0x0001
#define NTFS_FR_FLAG_FOLDER 0x0002

typedef struct tag_NTFS_FILERECORD {
    U32 Magic;                 // "FILE"
    U16 UpdateSequenceOffset;  //
    U16 UpdateSequenceSize;    // +1
    U16 SequenceNumber;
    U16 ReferenceCount;
    U16 SequenceOfAttributesOffset;  //
    U16 Flags;
    U32 RealSize;
    U32 AllocatedSize;
    U64 BaseRecord;
    U16 MaximumAttibuteID;  // +1
    U16 UpdateSequence;
    U16 UpdateSequenceArray[1];  // (UpdateSequenceSize - 1) elements
} NTFS_FILERECORD, *LPNTFS_FILERECORD;

/***************************************************************************/
// $VOLUME_NAME

typedef struct tag_NTFS_VOLUMENAME {
    U8 UnicodeName[1];
} NTFS_VOLUMENAME, *LPNTFS_VOLUMENAME;

/***************************************************************************/
// $VOLUME_INFORMATION

typedef struct tag_NTFS_VOLUMEINFO {
    U8 Unknown[8];
    U8 MajorVersion;
    U8 MinorVersion;
    U8 ChkDskFlag;
} NTFS_VOLUMEINFO, *LPNTFS_VOLUMEINFO;

/***************************************************************************/
// $AttrDef

typedef struct tag_NTFS_ATTRDEF {
    U8 Label[128];  // Unicode
    U64 Type;
    U64 Flags;
    U64 MinimumSize;
    U64 MaximumSize;
} NTFS_ATTRDEF, *LPNTFS_ATTRDEF;

/***************************************************************************/
// $STANDARD_INFORMATION
// Time is in 100-nanosecond intervals since Jan 1, 1601 (UTC)

typedef struct tag_NTFS_STDINFO {
    U64 CreationTime;
    U64 LastModTime;
    U64 FileRecordLastModTime;
    U64 LastAccessTime;
    U32 DOSFilePermissions;
    U8 Unknown[12];
} NTFS_STDINFO, *LPNTFS_STDINFO;

/***************************************************************************/

typedef struct tag_NTFS_VOLUME_GEOMETRY {
    U32 BytesPerSector;
    U32 SectorsPerCluster;
    U32 BytesPerCluster;
    U32 FileRecordSize;
    U64 MftStartCluster;
    STR VolumeLabel[MAX_FS_LOGICAL_NAME];
} NTFS_VOLUME_GEOMETRY, *LPNTFS_VOLUME_GEOMETRY;

/***************************************************************************/

typedef struct tag_NTFS_FILE_RECORD_INFO {
    U32 Index;
    U32 RecordSize;
    U32 UsedSize;
    U32 Flags;
    U32 SequenceNumber;
    U32 ReferenceCount;
    U32 SequenceOfAttributesOffset;
    U32 UpdateSequenceOffset;
    U32 UpdateSequenceSize;
    BOOL HasPrimaryFileName;
    U32 PrimaryFileNameNamespace;
    STR PrimaryFileName[MAX_FILE_NAME];
    DATETIME CreationTime;
    DATETIME LastModificationTime;
    DATETIME FileRecordModificationTime;
    DATETIME LastAccessTime;
    BOOL HasDataAttribute;
    BOOL DataIsResident;
    U64 DataSize;
    U64 AllocatedDataSize;
    U64 InitializedDataSize;
} NTFS_FILE_RECORD_INFO, *LPNTFS_FILE_RECORD_INFO;

/***************************************************************************/

#pragma pack(pop)

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
BOOL NtfsTimestampToDateTime(U64 NtfsTimestamp, LPDATETIME DateTime);

/***************************************************************************/

/**
 * @brief Mount an NTFS partition after validating its boot sector.
 *
 * @param Disk Physical disk pointer.
 * @param Partition Partition descriptor.
 * @param Base Base LBA offset.
 * @param PartIndex Partition index used for volume naming.
 * @return TRUE on success, FALSE when validation or allocation fails.
 */
BOOL MountPartition_NTFS(LPSTORAGE_UNIT Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex);

/***************************************************************************/

/**
 * @brief Retrieve cached NTFS geometry for a mounted volume.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Geometry Destination geometry structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL NtfsGetVolumeGeometry(LPFILESYSTEM FileSystem, LPNTFS_VOLUME_GEOMETRY Geometry);

/***************************************************************************/

/**
 * @brief Read one NTFS MFT file record and parse its base header.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT file record index.
 * @param RecordInfo Destination structure for parsed metadata.
 * @return TRUE on success, FALSE on read or validation failure.
 */
BOOL NtfsReadFileRecord(LPFILESYSTEM FileSystem, U32 Index, LPNTFS_FILE_RECORD_INFO RecordInfo);

/***************************************************************************/

/**
 * @brief Read default DATA stream of one NTFS file record by MFT index.
 *
 * Only the unnamed default stream is read. Alternate streams are ignored.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT file record index.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for number of bytes copied to Buffer.
 * @return TRUE on success, FALSE on read or validation failure.
 */
BOOL NtfsReadFileDataByIndex(
    LPFILESYSTEM FileSystem, U32 Index, LPVOID Buffer, U32 BufferSize, U32* BytesReadOut);

/***************************************************************************/

#endif  // NTFS_H_INCLUDED
