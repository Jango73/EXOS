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


    EXT2 (minimal in-memory implementation)

\************************************************************************/
#include "drivers/EXT2.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "String.h"

/************************************************************************/

#define VER_MAJOR 0
#define VER_MINOR 1

#define EXT2_DEFAULT_BLOCK_SIZE 1024
#define EXT2_INITIAL_FILE_CAPACITY 4

/************************************************************************/

typedef struct tag_EXT2FILESYSTEM {
    FILESYSTEM Header;
    LPPHYSICALDISK Disk;
    EXT2SUPER Super;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BlockSize;
    MUTEX FilesMutex;
    LPEXT2FILEREC* FileTable;
    U32 FileCount;
    U32 FileCapacity;
} EXT2FILESYSTEM, *LPEXT2FILESYSTEM;

/************************************************************************/

typedef struct tag_EXT2FILE {
    FILE Header;
    EXT2FILELOC Location;
} EXT2FILE, *LPEXT2FILE;

/************************************************************************/

static LPEXT2FILESYSTEM NewEXT2FileSystem(LPPHYSICALDISK Disk);
static LPEXT2FILE NewEXT2File(LPEXT2FILESYSTEM FileSystem, LPEXT2FILEREC Record);
static BOOL EnsureFileTableCapacity(LPEXT2FILESYSTEM FileSystem);
static LPEXT2FILEREC FindFileRecord(LPEXT2FILESYSTEM FileSystem, LPCSTR Name);
static LPEXT2FILEREC CreateFileRecord(LPEXT2FILESYSTEM FileSystem, LPCSTR Name);
static BOOL EnsureRecordCapacity(LPEXT2FILEREC Record, U32 RequiredSize);

static U32 Initialize(void);
static LPEXT2FILE OpenFile(LPFILEINFO Info);
static U32 CloseFile(LPEXT2FILE File);
static U32 ReadFile(LPEXT2FILE File);
static U32 WriteFile(LPEXT2FILE File);

BOOL MountPartition_EXT2(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex);
U32 EXT2Commands(U32 Function, U32 Parameter);

/************************************************************************/

DRIVER EXT2Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Jango73",
    .Product = "Minimal EXT2",
    .Command = EXT2Commands};

/************************************************************************/

static LPEXT2FILESYSTEM NewEXT2FileSystem(LPPHYSICALDISK Disk) {
    LPEXT2FILESYSTEM FileSystem;

    FileSystem = (LPEXT2FILESYSTEM)KernelHeapAlloc(sizeof(EXT2FILESYSTEM));
    if (FileSystem == NULL) return NULL;

    MemorySet(FileSystem, 0, sizeof(EXT2FILESYSTEM));

    FileSystem->Header.TypeID = KOID_FILESYSTEM;
    FileSystem->Header.References = 1;
    FileSystem->Header.Next = NULL;
    FileSystem->Header.Prev = NULL;
    FileSystem->Header.Driver = &EXT2Driver;
    FileSystem->Disk = Disk;
    FileSystem->PartitionStart = 0;
    FileSystem->PartitionSize = 0;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;
    FileSystem->FileTable = NULL;
    FileSystem->FileCount = 0;
    FileSystem->FileCapacity = 0;

    InitMutex(&(FileSystem->Header.Mutex));
    InitMutex(&(FileSystem->FilesMutex));

    return FileSystem;
}

/************************************************************************/

static LPEXT2FILE NewEXT2File(LPEXT2FILESYSTEM FileSystem, LPEXT2FILEREC Record) {
    LPEXT2FILE File;

    File = (LPEXT2FILE)KernelHeapAlloc(sizeof(EXT2FILE));
    if (File == NULL) return NULL;

    MemorySet(File, 0, sizeof(EXT2FILE));

    File->Header.TypeID = KOID_FILE;
    File->Header.References = 1;
    File->Header.Next = NULL;
    File->Header.Prev = NULL;
    File->Header.FileSystem = (LPFILESYSTEM)FileSystem;

    InitMutex(&(File->Header.Mutex));
    InitSecurity(&(File->Header.Security));

    File->Location.Record = Record;
    File->Location.Offset = 0;

    return File;
}

/************************************************************************/

static BOOL EnsureFileTableCapacity(LPEXT2FILESYSTEM FileSystem) {
    LPEXT2FILEREC* NewTable;
    U32 NewCapacity;
    U32 CopySize;

    if (FileSystem->FileCount < FileSystem->FileCapacity) return TRUE;

    if (FileSystem->FileCapacity == 0) {
        NewCapacity = EXT2_INITIAL_FILE_CAPACITY;
    } else {
        NewCapacity = FileSystem->FileCapacity * 2;
    }

    CopySize = sizeof(LPEXT2FILEREC) * FileSystem->FileCapacity;

    NewTable = (LPEXT2FILEREC*)KernelHeapAlloc(sizeof(LPEXT2FILEREC) * NewCapacity);
    if (NewTable == NULL) return FALSE;

    MemorySet(NewTable, 0, sizeof(LPEXT2FILEREC) * NewCapacity);

    if (FileSystem->FileTable != NULL && FileSystem->FileCapacity != 0) {
        MemoryCopy(NewTable, FileSystem->FileTable, CopySize);
        KernelHeapFree(FileSystem->FileTable);
    }

    FileSystem->FileTable = NewTable;
    FileSystem->FileCapacity = NewCapacity;

    return TRUE;
}

/************************************************************************/

static LPEXT2FILEREC FindFileRecord(LPEXT2FILESYSTEM FileSystem, LPCSTR Name) {
    U32 Index;

    if (FileSystem == NULL || Name == NULL) return NULL;

    for (Index = 0; Index < FileSystem->FileCount; Index++) {
        LPEXT2FILEREC Record = FileSystem->FileTable[Index];
        if (Record == NULL) continue;

        if (StringCompare(Record->Name, Name) == 0) {
            return Record;
        }
    }

    return NULL;
}

/************************************************************************/

static LPEXT2FILEREC CreateFileRecord(LPEXT2FILESYSTEM FileSystem, LPCSTR Name) {
    LPEXT2FILEREC Record;

    if (FileSystem == NULL || STRING_EMPTY(Name)) return NULL;

    if (EnsureFileTableCapacity(FileSystem) == FALSE) return NULL;

    Record = (LPEXT2FILEREC)KernelHeapAlloc(sizeof(EXT2FILEREC));
    if (Record == NULL) return NULL;

    MemorySet(Record, 0, sizeof(EXT2FILEREC));

    StringCopyLimit(Record->Name, Name, MAX_FILE_NAME - 1);
    Record->Attributes = 0;
    Record->Size = 0;
    Record->Capacity = 0;
    Record->Data = NULL;

    FileSystem->FileTable[FileSystem->FileCount++] = Record;

    return Record;
}

/************************************************************************/

static BOOL EnsureRecordCapacity(LPEXT2FILEREC Record, U32 RequiredSize) {
    U32 NewCapacity;
    U8* NewData;

    if (Record == NULL) return FALSE;
    if (RequiredSize <= Record->Capacity) return TRUE;

    if (Record->Capacity == 0) {
        NewCapacity = EXT2_DEFAULT_BLOCK_SIZE;
    } else {
        NewCapacity = Record->Capacity;
    }

    while (NewCapacity < RequiredSize) {
        NewCapacity *= 2;
    }

    NewData = (U8*)KernelHeapAlloc(NewCapacity);
    if (NewData == NULL) return FALSE;

    MemorySet(NewData, 0, NewCapacity);

    if (Record->Data != NULL) {
        if (Record->Size > 0) {
            MemoryCopy(NewData, Record->Data, Record->Size);
        }

        KernelHeapFree(Record->Data);
    }

    Record->Data = NewData;
    Record->Capacity = NewCapacity;

    return TRUE;
}

/************************************************************************/

static U32 Initialize(void) { return DF_ERROR_SUCCESS; }

/************************************************************************/

static LPEXT2FILE OpenFile(LPFILEINFO Info) {
    LPEXT2FILESYSTEM FileSystem;
    LPEXT2FILEREC Record;
    LPEXT2FILE File;

    if (Info == NULL || STRING_EMPTY(Info->Name)) return NULL;

    FileSystem = (LPEXT2FILESYSTEM)Info->FileSystem;
    if (FileSystem == NULL) return NULL;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    Record = FindFileRecord(FileSystem, Info->Name);
    if (Record == NULL && (Info->Flags & FILE_OPEN_CREATE_ALWAYS)) {
        Record = CreateFileRecord(FileSystem, Info->Name);
    }

    if (Record == NULL) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return NULL;
    }

    if (Info->Flags & FILE_OPEN_TRUNCATE) {
        Record->Size = 0;
        if (Record->Data != NULL) {
            MemorySet(Record->Data, 0, Record->Capacity);
        }
    }

    File = NewEXT2File(FileSystem, Record);
    if (File == NULL) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return NULL;
    }

    StringCopy(File->Header.Name, Record->Name);
    File->Header.OpenFlags = Info->Flags;
    File->Header.Attributes = Record->Attributes;
    File->Header.SizeLow = Record->Size;
    File->Header.SizeHigh = 0;
    File->Header.Position = (Info->Flags & FILE_OPEN_APPEND) ? Record->Size : 0;
    File->Header.BytesTransferred = 0;

    UnlockMutex(&(FileSystem->FilesMutex));

    return File;
}

/************************************************************************/

static U32 CloseFile(LPEXT2FILE File) {
    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;

    KernelHeapFree(File);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 ReadFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    LPEXT2FILEREC Record;
    U32 Available;
    U32 ToTransfer;

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_ERROR_NOPERM;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_ERROR_BADPARAM;

    Record = File->Location.Record;
    if (Record == NULL) return DF_ERROR_BADPARAM;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    File->Header.BytesTransferred = 0;

    if (File->Header.Position >= Record->Size) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_SUCCESS;
    }

    Available = Record->Size - File->Header.Position;
    ToTransfer = File->Header.ByteCount;
    if (ToTransfer > Available) {
        ToTransfer = Available;
    }

    if (ToTransfer > 0) {
        MemoryCopy(File->Header.Buffer, Record->Data + File->Header.Position, ToTransfer);
        File->Header.Position += ToTransfer;
        File->Header.BytesTransferred = ToTransfer;
    }

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 WriteFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    LPEXT2FILEREC Record;
    U32 RequiredSize;

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    if ((File->Header.OpenFlags & FILE_OPEN_WRITE) == 0) {
        return DF_ERROR_NOPERM;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_ERROR_BADPARAM;

    Record = File->Location.Record;
    if (Record == NULL) return DF_ERROR_BADPARAM;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    if (File->Header.OpenFlags & FILE_OPEN_APPEND) {
        File->Header.Position = Record->Size;
    }

    File->Header.BytesTransferred = 0;

    if (File->Header.ByteCount == 0) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_SUCCESS;
    }

    RequiredSize = File->Header.Position + File->Header.ByteCount;

    if (EnsureRecordCapacity(Record, RequiredSize) == FALSE) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_NOMEMORY;
    }

    if (File->Header.Position > Record->Size) {
        MemorySet(Record->Data + Record->Size, 0, File->Header.Position - Record->Size);
    }

    MemoryCopy(Record->Data + File->Header.Position, File->Header.Buffer, File->Header.ByteCount);

    File->Header.Position += File->Header.ByteCount;
    File->Header.BytesTransferred = File->Header.ByteCount;

    if (File->Header.Position > Record->Size) {
        Record->Size = File->Header.Position;
    }

    File->Header.SizeLow = Record->Size;

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

BOOL MountPartition_EXT2(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE * 2];
    IOCONTROL Control;
    LPEXT2SUPER Super;
    LPEXT2FILESYSTEM FileSystem;
    U32 Result;
    SECTOR PartitionStart;

    if (Disk == NULL || Partition == NULL) return FALSE;

    PartitionStart = Base + Partition->LBA;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = PartitionStart + 2;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = sizeof(Buffer);

    Result = Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    Super = (LPEXT2SUPER)Buffer;

    if (Super->Magic != EXT2_SUPER_MAGIC) {
        DEBUG(TEXT("[MountPartition_EXT2] Invalid superblock magic: %04X"), Super->Magic);
        return FALSE;
    }

    FileSystem = NewEXT2FileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    MemoryCopy(&(FileSystem->Super), Super, sizeof(EXT2SUPER));

    FileSystem->PartitionStart = PartitionStart;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;

    if (Super->LogBlockSize <= 4) {
        FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE << Super->LogBlockSize;
    }

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    ListAddItem(Kernel.FileSystem, FileSystem);

    DEBUG(TEXT("[MountPartition_EXT2] Mounted EXT2 volume %s (block size %u)"),
        FileSystem->Header.Name, FileSystem->BlockSize);

    return TRUE;
}

/************************************************************************/

U32 EXT2Commands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_OPENFILE:
            return (U32)OpenFile((LPFILEINFO)Parameter);
        case DF_FS_CLOSEFILE:
            return CloseFile((LPEXT2FILE)Parameter);
        case DF_FS_READ:
            return ReadFile((LPEXT2FILE)Parameter);
        case DF_FS_WRITE:
            return WriteFile((LPEXT2FILE)Parameter);
        default:
            break;
    }

    return DF_ERROR_NOTIMPL;
}

/************************************************************************/
