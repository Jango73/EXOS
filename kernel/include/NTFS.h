
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

#pragma pack(1)

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
// Time is nanoseconds since Jan 1, 1601

typedef struct tag_NTFS_STDINFO {
    U64 CreationTime;
    U64 LastModTime;
    U64 FileRecordLastModTime;
    U64 LastAccessTime;
    U32 DOSFilePermissions;
    U8 Unknown[12];
} NTFS_STDINFO, *LPNTFS_STDINFO;

/***************************************************************************/

#endif  // NTFS_H_INCLUDED
