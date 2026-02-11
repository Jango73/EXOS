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


    NTFS folder index traversal

\************************************************************************/

#include "NTFS-Private.h"

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
    U32 AllocatedEntrySize;
    U32 Cursor;

    if (Context == NULL || Header == NULL || PendingCountInOut == NULL) return FALSE;
    if (HeaderRegionSize < sizeof(NTFS_INDEX_HEADER)) return FALSE;

    EntryOffset = NtfsLoadU32((const U8*)Header);
    EntrySize = NtfsLoadU32(((const U8*)Header) + 4);
    AllocatedEntrySize = NtfsLoadU32(((const U8*)Header) + 8);

    if (EntryOffset > HeaderRegionSize) {
        // Some volumes expose entry offsets relative to the INDX record start.
        if (EntryOffset >= 24 && (EntryOffset - 24) <= HeaderRegionSize) {
            EntryOffset -= 24;
        } else {
            WARNING(TEXT("[NtfsTraverseIndexHeader] Invalid entry offset (offset=%u, region=%u)"),
                    EntryOffset,
                    HeaderRegionSize);
            return FALSE;
        }
    }

    if (EntrySize > (HeaderRegionSize - EntryOffset)) {
        if (EntrySize >= EntryOffset && EntrySize <= HeaderRegionSize) {
            // Some NTFS index headers encode EntrySize as an absolute end offset
            // from the beginning of the header region.
            EntrySize -= EntryOffset;
        } else
        if (AllocatedEntrySize != 0 &&
            AllocatedEntrySize <= (HeaderRegionSize - EntryOffset) &&
            EntrySize > AllocatedEntrySize) {
            EntrySize = AllocatedEntrySize;
        } else {
            WARNING(TEXT("[NtfsTraverseIndexHeader] Clamping entry size (size=%u, offset=%u, region=%u)"),
                    EntrySize,
                    EntryOffset,
                    HeaderRegionSize);
            EntrySize = HeaderRegionSize - EntryOffset;
        }
    }

    if (EntrySize < 16) {
        WARNING(TEXT("[NtfsTraverseIndexHeader] Invalid entry size after normalization (%u)"), EntrySize);
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
