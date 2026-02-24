
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


    Shell commands

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "utils/SizeFormat.h"

static U64 ShellSectorCountToBytes(U32 SectorCount) {
#ifdef __EXOS_32__
    return U64_Make(SectorCount >> 23, SectorCount << 9);
#else
    return ((U64)SectorCount << 9);
#endif
}

/***************************************************************************/

/**
 * @brief Print one shell line with an auto-scaled byte size.
 * @param Label Left column label.
 * @param ByteCount Size in bytes.
 */
static void ShellPrintByteSizeLine(LPCSTR Label, U64 ByteCount) {
    STR SizeText[32];

    SizeFormatBytesText(ByteCount, SizeText);
    ConsolePrint(TEXT("%s: %s\n"), Label, SizeText);
}

/***************************************************************************/
U32 CMD_sysinfo(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    SYSTEMINFO Info;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    DoSystemCall(SYSCALL_GetSystemInfo, SYSCALL_PARAM(&Info));

    ShellPrintByteSizeLine(TEXT("Total physical memory     "), Info.TotalPhysicalMemory);
    ShellPrintByteSizeLine(TEXT("Physical memory used      "), Info.PhysicalMemoryUsed);
    ShellPrintByteSizeLine(TEXT("Physical memory available "), Info.PhysicalMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Total swap memory         "), Info.TotalSwapMemory);
    ShellPrintByteSizeLine(TEXT("Swap memory used          "), Info.SwapMemoryUsed);
    ShellPrintByteSizeLine(TEXT("Swap memory available     "), Info.SwapMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Total memory available    "), Info.TotalMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Processor page size       "), U64_FromUINT(Info.PageSize));
    ConsolePrint(TEXT("Total physical pages      : %u pages\n"), Info.TotalPhysicalPages);
    ConsolePrint(TEXT("Minimum linear address    : %x\n"), Info.MinimumLinearAddress);
    ConsolePrint(TEXT("Maximum linear address    : %x\n"), Info.MaximumLinearAddress);
    ConsolePrint(TEXT("User name                 : %s\n"), Info.UserName);
    ConsolePrint(TEXT("Number of processes       : %d\n"), Info.NumProcesses);
    ConsolePrint(TEXT("Number of tasks           : %d\n"), Info.NumTasks);
    ConsolePrint(TEXT("Keyboard layout           : %s\n"), Info.KeyboardLayout);

    TEST(TEXT("[CMD_sysinfo] sys_info : OK"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_cat(LPSHELLCONTEXT Context) {
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR FileName[MAX_PATH_NAME];
    HANDLE Handle;
    U32 FileSize;
    U8* Buffer;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
            FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
            FileOpenInfo.Header.Flags = 0;
            FileOpenInfo.Name = FileName;
            FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

            Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));

            if (Handle) {
                FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));

                if (FileSize) {
                    Buffer = (U8*)HeapAlloc(FileSize + 1);

                    if (Buffer) {
                        FileOperation.Header.Size = sizeof(FILEOPERATION);
                        FileOperation.Header.Version = EXOS_ABI_VERSION;
                        FileOperation.Header.Flags = 0;
                        FileOperation.File = Handle;
                        FileOperation.NumBytes = FileSize;
                        FileOperation.Buffer = Buffer;

                        if (DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation))) {
                            Buffer[FileSize] = STR_NULL;
                            ConsolePrint((LPSTR)Buffer);
                            Success = TRUE;
                        }

                        HeapFree(Buffer);
                    }
                }
                DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
            }
        }
    }

    if (Success) {
        TEST(TEXT("[CMD_type] type %s : OK"), FileName);
    } else {
        TEST(TEXT("[CMD_type] type : KO"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_copy(LPSHELLCONTEXT Context) {
    STR SrcName[MAX_PATH_NAME];
    STR DstName[MAX_PATH_NAME];
    LPVOID SourceBytes = NULL;
    UINT FileSize = 0;
    UINT TotalCopied = 0;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return DF_RETURN_SUCCESS;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return DF_RETURN_SUCCESS;

    ConsolePrint(TEXT("%s %s\n"), SrcName, DstName);

    SourceBytes = FileReadAll(SrcName, &FileSize);
    if (SourceBytes != NULL) {
        TotalCopied = FileWriteAll(DstName, SourceBytes, FileSize);
        KernelHeapFree(SourceBytes);
    }

    Success = (TotalCopied == FileSize);
    DEBUG(TEXT("[CMD_copy] TotalCopied=%u FileSize=%u"), TotalCopied, FileSize);

    if (Success) {
        TEST(TEXT("[CMD_copy] copy %s %s : OK"), SrcName, DstName);
    } else {
        TEST(TEXT("[CMD_copy] copy %s %s : KO"), SrcName, DstName);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_edit(LPSHELLCONTEXT Context) {
    LPSTR Arguments[2];
    STR FileName[MAX_PATH_NAME];
    BOOL HasArgument = FALSE;
    BOOL ArgumentProvided = FALSE;
    BOOL LineNumbers;

    FileName[0] = STR_NULL;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        ArgumentProvided = TRUE;
        if (QualifyFileName(Context, Context->Command, FileName)) {
            Arguments[0] = FileName;
            HasArgument = TRUE;
        }
    }

    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }

    LineNumbers = HasOption(Context, TEXT("n"), TEXT("line_numbers"));

    if (HasArgument) {
        Edit(1, (LPCSTR*)Arguments, LineNumbers);
    } else if (!ArgumentProvided) {
        Edit(0, NULL, LineNumbers);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_disk(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPSTORAGE_UNIT Disk;
    DISKINFO DiskInfo;

    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
        STR SizeText[32];
        Disk = (LPSTORAGE_UNIT)Node;

        DiskInfo.Disk = Disk;
        Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);
        SizeFormatBytesText(U64_FromUINT(DiskInfo.BytesPerSector), SizeText);

        ConsolePrint(TEXT("Manufacturer : %s\n"), Disk->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), Disk->Driver->Product);
        ConsolePrint(TEXT("Sector size  : %s\n"), SizeText);
        ConsolePrint(TEXT("Sectors      : %x%08x\n"),
                     (U32)U64_High32(DiskInfo.NumSectors),
                     (U32)U64_Low32(DiskInfo.NumSectors));
        ConsolePrint(TEXT("\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_filesystem(LPSHELLCONTEXT Context) {
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;
    BOOL LongMode;

    ParseNextCommandLineComponent(Context);
    LongMode = HasOption(Context, TEXT("l"), TEXT("long"));

    if (StringLength(Context->Command) != 0) {
        ConsolePrint(TEXT("Usage: fs [--long]\n"));
        return DF_RETURN_SUCCESS;
    }

    if (LongMode) {
        ConsolePrint(TEXT("General information\n"));
        FILESYSTEM_GLOBAL_INFO* FileSystemInfo = GetFileSystemGlobalInfo();

        if (StringEmpty(FileSystemInfo->ActivePartitionName) == FALSE) {
            ConsolePrint(TEXT("Active partition : %s\n"), FileSystemInfo->ActivePartitionName);
        } else {
            ConsolePrint(TEXT("Active partition : <none>\n"));
        }

        ConsolePrint(TEXT("\n"));
        ConsolePrint(TEXT("Discovered file systems\n"));
    } else {
        ConsolePrint(TEXT("%-12s %-12s %-10s %11s\n"),
            TEXT("Name"), TEXT("Type"), TEXT("Format"), TEXT("Size"));
        ConsolePrint(TEXT("-------------------------------------------------\n"));
    }

    U32 UnmountedCount = 0;
    LPLIST Lists[2] = {GetFileSystemList(), GetUnusedFileSystemList()};
    for (U32 ListIndex = 0; ListIndex < 2; ListIndex++) {
        LPLIST FileSystemList = Lists[ListIndex];
        for (Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node; Node = Node->Next) {
            DISKINFO DiskInfo;
            BOOL DiskInfoValid = FALSE;
            LPSTORAGE_UNIT StorageUnit;
            U64 PartitionSizeBytes;
            STR PartitionSizeText[32];

            FileSystem = (LPFILESYSTEM)Node;
            StorageUnit = FileSystemGetStorageUnit(FileSystem);
            PartitionSizeBytes = ShellSectorCountToBytes(FileSystem->Partition.NumSectors);
            SizeFormatBytesText(PartitionSizeBytes, PartitionSizeText);

            if (FileSystem->Mounted == FALSE) {
                UnmountedCount++;
            }

            if (!LongMode) {
                STR DisplayName[MAX_FS_LOGICAL_NAME + 2];
                StringCopy(DisplayName, FileSystem->Name);
                if (FileSystem->Mounted == FALSE) {
                    StringConcat(DisplayName, TEXT("*"));
                }

                ConsolePrint(TEXT("%-12s %-12s %-10s %11s\n"),
                    DisplayName,
                    FileSystemGetPartitionTypeName(&FileSystem->Partition),
                    FileSystemGetPartitionFormatName(FileSystem->Partition.Format),
                    PartitionSizeText);
                continue;
            }

            ConsolePrint(TEXT("Name         : %s\n"), FileSystem->Name);
            ConsolePrint(TEXT("Mounted      : %s\n"), FileSystem->Mounted ? TEXT("YES") : TEXT("NO"));
            if (FileSystem->Driver != NULL) {
                ConsolePrint(TEXT("FS driver    : %s / %s\n"), FileSystem->Driver->Manufacturer, FileSystem->Driver->Product);
            } else {
                ConsolePrint(TEXT("FS driver    : <none>\n"));
            }
            ConsolePrint(TEXT("Scheme       : %s\n"), FileSystemGetPartitionSchemeName(FileSystem->Partition.Scheme));
            ConsolePrint(TEXT("Type         : %s\n"), FileSystemGetPartitionTypeName(&FileSystem->Partition));
            ConsolePrint(TEXT("Format       : %s\n"), FileSystemGetPartitionFormatName(FileSystem->Partition.Format));
            if (FileSystem->Partition.Format == PARTITION_FORMAT_NTFS) {
                NTFS_VOLUME_GEOMETRY Geometry;
                MemorySet(&Geometry, 0, sizeof(NTFS_VOLUME_GEOMETRY));
                if (NtfsGetVolumeGeometry(FileSystem, &Geometry)) {
                    STR GeometrySizeText[32];

                    SizeFormatBytesText(U64_FromUINT(Geometry.BytesPerSector), GeometrySizeText);
                    ConsolePrint(TEXT("NTFS bytes/sector   : %s\n"), GeometrySizeText);
                    ConsolePrint(TEXT("NTFS sectors/cluster: %u\n"), Geometry.SectorsPerCluster);
                    SizeFormatBytesText(U64_FromUINT(Geometry.BytesPerCluster), GeometrySizeText);
                    ConsolePrint(TEXT("NTFS bytes/cluster  : %s\n"), GeometrySizeText);
                    SizeFormatBytesText(U64_FromUINT(Geometry.FileRecordSize), GeometrySizeText);
                    ConsolePrint(TEXT("NTFS record size    : %s\n"), GeometrySizeText);
                    ConsolePrint(TEXT("NTFS MFT LCN : %x, %x\n"),
                        (U32)U64_High32(Geometry.MftStartCluster),
                        (U32)U64_Low32(Geometry.MftStartCluster));
                    if (StringEmpty(Geometry.VolumeLabel)) {
                        ConsolePrint(TEXT("NTFS label   : <unknown>\n"));
                    } else {
                        ConsolePrint(TEXT("NTFS label   : %s\n"), Geometry.VolumeLabel);
                    }
                }
            }
            ConsolePrint(TEXT("Index        : %u\n"), FileSystem->Partition.Index);
            ConsolePrint(TEXT("Start sector : %u\n"), FileSystem->Partition.StartSector);
            ConsolePrint(TEXT("Size         : %u sectors (%s)\n"),
                FileSystem->Partition.NumSectors, PartitionSizeText);
            ConsolePrint(TEXT("Active       : %s\n"),
                (FileSystem->Partition.Flags & PARTITION_FLAG_ACTIVE) ? TEXT("YES") : TEXT("NO"));

            if (FileSystem->Partition.Scheme == PARTITION_SCHEME_MBR) {
                ConsolePrint(TEXT("Type id      : %x\n"), FileSystem->Partition.Type);
            } else if (FileSystem->Partition.Scheme == PARTITION_SCHEME_GPT) {
                ConsolePrint(TEXT("Type GUID    : %x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x\n"),
                    FileSystem->Partition.TypeGuid[0], FileSystem->Partition.TypeGuid[1],
                    FileSystem->Partition.TypeGuid[2], FileSystem->Partition.TypeGuid[3],
                    FileSystem->Partition.TypeGuid[4], FileSystem->Partition.TypeGuid[5],
                    FileSystem->Partition.TypeGuid[6], FileSystem->Partition.TypeGuid[7],
                    FileSystem->Partition.TypeGuid[8], FileSystem->Partition.TypeGuid[9],
                    FileSystem->Partition.TypeGuid[10], FileSystem->Partition.TypeGuid[11],
                    FileSystem->Partition.TypeGuid[12], FileSystem->Partition.TypeGuid[13],
                    FileSystem->Partition.TypeGuid[14], FileSystem->Partition.TypeGuid[15]);
            }

            if (StorageUnit != NULL && StorageUnit->Driver != NULL) {
                MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
                DiskInfo.Disk = StorageUnit;
                if (StorageUnit->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo) == DF_RETURN_SUCCESS) {
                    DiskInfoValid = TRUE;
                }
                ConsolePrint(TEXT("Storage      : %s / %s\n"),
                    StorageUnit->Driver->Manufacturer, StorageUnit->Driver->Product);
            } else {
                ConsolePrint(TEXT("Storage      : <none>\n"));
            }

            if (DiskInfoValid) {
                ConsolePrint(TEXT("Removable    : %s\n"), DiskInfo.Removable ? TEXT("YES") : TEXT("NO"));
                ConsolePrint(TEXT("Read only    : %s\n"),
                    (DiskInfo.Access & DISK_ACCESS_READONLY) ? TEXT("YES") : TEXT("NO"));
                ConsolePrint(TEXT("Disk sectors : %x, %x\n"),
                    (U32)U64_High32(DiskInfo.NumSectors),
                    (U32)U64_Low32(DiskInfo.NumSectors));
            }
            ConsolePrint(TEXT("\n"));
        }
    }

    if (!LongMode && UnmountedCount > 0) {
        ConsolePrint(TEXT("\n"));
        ConsolePrint(TEXT("* = unmounted\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

