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


    NTFS file-record and data stream logic

\************************************************************************/

#include "NTFS-Private.h"

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
BOOL NtfsLoadFileRecordBuffer(
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
BOOL NtfsReadNonResidentDataAttribute(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut) {
    return NtfsReadNonResidentDataAttributeRange(
        FileSystem,
        DataAttribute,
        DataAttributeLength,
        U64_Make(0, 0),
        Buffer,
        BufferSize,
        DataSize,
        BytesReadOut);
}

/***************************************************************************/

/**
 * @brief Read one non-resident DATA stream range using runlist mapping.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param DataAttribute Pointer to DATA attribute header.
 * @param DataAttributeLength DATA attribute length.
 * @param DataOffset Stream start offset in bytes.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param DataSize Logical data size in bytes.
 * @param BytesReadOut Output number of bytes copied in Buffer.
 * @return TRUE on success, FALSE on malformed runlist or read failure.
 */
BOOL NtfsReadNonResidentDataAttributeRange(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    U64 DataOffset,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut) {
    U16 RunListOffset;
    const U8* RunPointer;
    const U8* RunEnd;
    U32 TargetBytes;
    U32 BytesWritten;
    U64 RemainingOffset;
    U32 BytesPerSector;
    I32 CurrentLcn;
    U8* SectorBuffer;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || DataAttribute == NULL || Buffer == NULL) return FALSE;
    if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) return FALSE;
    if (U64_Cmp(DataOffset, DataSize) >= 0) return TRUE;
    if (FileSystem->BytesPerSector == 0) return FALSE;

    BytesPerSector = FileSystem->BytesPerSector;
    RemainingOffset = DataOffset;

    TargetBytes = BufferSize;
    {
        U64 RemainingData = U64_Sub(DataSize, DataOffset);
        if (U64_High32(RemainingData) == 0 && U64_Low32(RemainingData) < TargetBytes) {
            TargetBytes = U64_Low32(RemainingData);
        }
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
    SectorBuffer = NULL;

    while (RunPointer < RunEnd && BytesWritten < TargetBytes) {
        U8 Header;
        U32 LengthSize;
        U32 OffsetSize;
        U64 ClusterCount64;
        U32 ClusterCount;
        I32 LcnDelta;
        BOOL IsSparse;
        U32 RunBytes;
        U64 RunBytes64;
        U32 SkipInRun;
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

        RunBytes = 0;
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
        RunBytes64 = U64_FromU32(RunBytes);
        if (U64_Cmp(RemainingOffset, RunBytes64) >= 0) {
            RemainingOffset = U64_Sub(RemainingOffset, RunBytes64);
            continue;
        }

        SkipInRun = U64_Low32(RemainingOffset);
        RemainingOffset = U64_Make(0, 0);

        CopyBytes = TargetBytes - BytesWritten;
        if (CopyBytes > (RunBytes - SkipInRun)) {
            CopyBytes = RunBytes - SkipInRun;
        }
        if (CopyBytes == 0) continue;

        if (IsSparse) {
            MemorySet((U8*)Buffer + BytesWritten, 0, CopyBytes);
        } else {
            U32 ClusterLcn;
            U32 SectorOffset;
            U32 StartSector;
            U32 RelativeSector;
            U32 OffsetInSector;
            U32 RemainingCopy;
            U32 DestinationOffset;

            if (CurrentLcn < 0) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Invalid LCN"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }

            ClusterLcn = (U32)CurrentLcn;
            if (ClusterLcn > 0xFFFFFFFF / FileSystem->SectorsPerCluster) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] LCN sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }

            SectorOffset = ClusterLcn * FileSystem->SectorsPerCluster;
            RelativeSector = SkipInRun / BytesPerSector;
            OffsetInSector = SkipInRun % BytesPerSector;
            if (SectorOffset > 0xFFFFFFFF - RelativeSector) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Partition sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }
            SectorOffset += RelativeSector;
            if (FileSystem->PartitionStart > 0xFFFFFFFF - SectorOffset) {
                WARNING(TEXT("[NtfsReadNonResidentDataAttribute] Partition sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }
            StartSector = FileSystem->PartitionStart + SectorOffset;

            if (SectorBuffer == NULL) {
                SectorBuffer = (U8*)KernelHeapAlloc(BytesPerSector);
                if (SectorBuffer == NULL) {
                    ERROR(TEXT("[NtfsReadNonResidentDataAttribute] Unable to allocate sector buffer"));
                    return FALSE;
                }
            }

            RemainingCopy = CopyBytes;
            DestinationOffset = BytesWritten;
            while (RemainingCopy > 0) {
                U32 Chunk = BytesPerSector - OffsetInSector;
                if (Chunk > RemainingCopy) Chunk = RemainingCopy;

                if (!NtfsReadSectors(FileSystem, StartSector, 1, SectorBuffer, BytesPerSector)) {
                    KernelHeapFree(SectorBuffer);
                    return FALSE;
                }

                MemoryCopy(
                    ((U8*)Buffer) + DestinationOffset,
                    SectorBuffer + OffsetInSector,
                    Chunk);

                DestinationOffset += Chunk;
                RemainingCopy -= Chunk;
                StartSector++;
                OffsetInSector = 0;
            }
        }

        BytesWritten += CopyBytes;
    }

    if (SectorBuffer != NULL) {
        KernelHeapFree(SectorBuffer);
    }

    if (BytesReadOut != NULL) *BytesReadOut = BytesWritten;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read one NTFS default DATA stream range by file-record index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param Offset Start offset within stream.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for bytes copied to Buffer.
 * @return TRUE on success, FALSE on malformed attributes or read failure.
 */
BOOL NtfsReadFileDataRangeByIndex(
    LPFILESYSTEM FileSystem,
    U32 Index,
    U64 Offset,
    LPVOID Buffer,
    U32 BufferSize,
    U32* BytesReadOut) {
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

        if (U64_Cmp(Offset, RecordInfo.DataSize) >= 0) {
            KernelHeapFree(RecordBuffer);
            if (BytesReadOut != NULL) *BytesReadOut = 0;
            return TRUE;
        }

        DataAttribute = RecordBuffer + DataAttributeOffset;
        if (RecordInfo.DataIsResident) {
            U32 StartOffset;

            if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }

            if (U64_High32(Offset) != 0) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }
            StartOffset = U64_Low32(Offset);

            ValueLength = NtfsLoadU32(DataAttribute + 16);
            ValueOffset = NtfsLoadU16(DataAttribute + 20);
            if (ValueOffset > DataAttributeLength || ValueLength > (DataAttributeLength - ValueOffset)) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }
            if (StartOffset >= ValueLength) {
                KernelHeapFree(RecordBuffer);
                return TRUE;
            }

            BytesToCopy = ValueLength - StartOffset;
            if (BytesToCopy > BufferSize) BytesToCopy = BufferSize;
            if (BytesToCopy > 0) {
                MemoryCopy(Buffer, DataAttribute + ValueOffset + StartOffset, BytesToCopy);
            }
            if (BytesReadOut != NULL) *BytesReadOut = BytesToCopy;
            KernelHeapFree(RecordBuffer);
            return TRUE;
        }

        if (!NtfsReadNonResidentDataAttributeRange(
                NtfsFileSystem,
                DataAttribute,
                DataAttributeLength,
                Offset,
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
    return NtfsReadFileDataRangeByIndex(
        FileSystem,
        Index,
        U64_Make(0, 0),
        Buffer,
        BufferSize,
        BytesReadOut);
}
