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


    Script Exposure Helpers - File System

\************************************************************************/

#include "Exposed.h"

#include "fs/FileSystem.h"
#include "KernelData.h"

/************************************************************************/

static int DATA_SECTION FileSystemRootSentinel = 0;

SCRIPT_HOST_HANDLE FileSystemRootHandle = &FileSystemRootSentinel;

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed file system root object.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemRootGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_STRING("active_partition_name", GetFileSystemGlobalInfo()->ActivePartitionName);
    EXPOSE_BIND_HOST_HANDLE("mounted", GetFileSystemList(), &FileSystemArrayDescriptor, NULL);
    EXPOSE_BIND_HOST_HANDLE("unused", GetUnusedFileSystemList(), &FileSystemArrayDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from an exposed file system object.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    DISKINFO DiskInfo;
    LPSTORAGE_UNIT StorageUnit = NULL;
    BOOL DiskInfoValid = FALSE;

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPFILESYSTEM FileSystem = (LPFILESYSTEM)Parent;
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        StorageUnit = FileSystemGetStorageUnit(FileSystem);

        EXPOSE_BIND_STRING("name", FileSystem->Name);
        EXPOSE_BIND_INTEGER("mounted", FileSystem->Mounted);
        EXPOSE_BIND_STRING("scheme_name", FileSystemGetPartitionSchemeName(FileSystem->Partition.Scheme));
        EXPOSE_BIND_STRING("type_name", FileSystemGetPartitionTypeName(&FileSystem->Partition));
        EXPOSE_BIND_STRING("format_name", FileSystemGetPartitionFormatName(FileSystem->Partition.Format));
        EXPOSE_BIND_INTEGER("scheme", FileSystem->Partition.Scheme);
        EXPOSE_BIND_INTEGER("type", FileSystem->Partition.Type);
        EXPOSE_BIND_INTEGER("format", FileSystem->Partition.Format);
        EXPOSE_BIND_INTEGER("index", FileSystem->Partition.Index);
        EXPOSE_BIND_INTEGER("flags", FileSystem->Partition.Flags);
        EXPOSE_BIND_INTEGER("start_sector", FileSystem->Partition.StartSector);
        EXPOSE_BIND_INTEGER("num_sectors", FileSystem->Partition.NumSectors);
        EXPOSE_BIND_INTEGER("type_guid_0", FileSystem->Partition.TypeGuid[0]);
        EXPOSE_BIND_INTEGER("type_guid_1", FileSystem->Partition.TypeGuid[1]);
        EXPOSE_BIND_INTEGER("type_guid_2", FileSystem->Partition.TypeGuid[2]);
        EXPOSE_BIND_INTEGER("type_guid_3", FileSystem->Partition.TypeGuid[3]);
        EXPOSE_BIND_INTEGER("type_guid_4", FileSystem->Partition.TypeGuid[4]);
        EXPOSE_BIND_INTEGER("type_guid_5", FileSystem->Partition.TypeGuid[5]);
        EXPOSE_BIND_INTEGER("type_guid_6", FileSystem->Partition.TypeGuid[6]);
        EXPOSE_BIND_INTEGER("type_guid_7", FileSystem->Partition.TypeGuid[7]);
        EXPOSE_BIND_INTEGER("type_guid_8", FileSystem->Partition.TypeGuid[8]);
        EXPOSE_BIND_INTEGER("type_guid_9", FileSystem->Partition.TypeGuid[9]);
        EXPOSE_BIND_INTEGER("type_guid_10", FileSystem->Partition.TypeGuid[10]);
        EXPOSE_BIND_INTEGER("type_guid_11", FileSystem->Partition.TypeGuid[11]);
        EXPOSE_BIND_INTEGER("type_guid_12", FileSystem->Partition.TypeGuid[12]);
        EXPOSE_BIND_INTEGER("type_guid_13", FileSystem->Partition.TypeGuid[13]);
        EXPOSE_BIND_INTEGER("type_guid_14", FileSystem->Partition.TypeGuid[14]);
        EXPOSE_BIND_INTEGER("type_guid_15", FileSystem->Partition.TypeGuid[15]);

        if (FileSystem->Driver != NULL) {
            EXPOSE_BIND_STRING("driver_manufacturer", FileSystem->Driver->Manufacturer);
            EXPOSE_BIND_STRING("driver_product", FileSystem->Driver->Product);
        } else {
            EXPOSE_BIND_STRING("driver_manufacturer", TEXT(""));
            EXPOSE_BIND_STRING("driver_product", TEXT(""));
        }

        EXPOSE_BIND_INTEGER("has_storage", StorageUnit != NULL);
        if (StorageUnit != NULL && StorageUnit->Driver != NULL) {
            MemorySet(&DiskInfo, 0, sizeof(DiskInfo));
            DiskInfo.Disk = StorageUnit;
            DiskInfoValid = (StorageUnit->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo) == DF_RETURN_SUCCESS);

            EXPOSE_BIND_STRING("storage_manufacturer", StorageUnit->Driver->Manufacturer);
            EXPOSE_BIND_STRING("storage_product", StorageUnit->Driver->Product);
            EXPOSE_BIND_INTEGER("removable", DiskInfoValid ? DiskInfo.Removable : 0);
            EXPOSE_BIND_INTEGER("read_only", DiskInfoValid ? ((DiskInfo.Access & DISK_ACCESS_READONLY) != 0) : 0);
            EXPOSE_BIND_INTEGER("disk_num_sectors_low", DiskInfoValid ? (U32)U64_Low32(DiskInfo.NumSectors) : 0);
            EXPOSE_BIND_INTEGER("disk_num_sectors_high", DiskInfoValid ? (U32)U64_High32(DiskInfo.NumSectors) : 0);
        } else {
            EXPOSE_BIND_STRING("storage_manufacturer", TEXT(""));
            EXPOSE_BIND_STRING("storage_product", TEXT(""));
            EXPOSE_BIND_INTEGER("removable", 0);
            EXPOSE_BIND_INTEGER("read_only", 0);
            EXPOSE_BIND_INTEGER("disk_num_sectors_low", 0);
            EXPOSE_BIND_INTEGER("disk_num_sectors_high", 0);
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed file system array.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST FileSystemList = (LPLIST)Parent;
    if (FileSystemList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(FileSystemList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one file system from the exposed file system array.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting file system handle
 * @return SCRIPT_OK when the file system exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST FileSystemList = (LPLIST)Parent;
    if (FileSystemList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(FileSystemList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPFILESYSTEM FileSystem = (LPFILESYSTEM)ListGetItem(FileSystemList, Index);
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        EXPOSE_SET_HOST_HANDLE(FileSystem, &FileSystemDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR FileSystemRootDescriptor = {
    FileSystemRootGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR FileSystemDescriptor = {
    FileSystemGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR FileSystemArrayDescriptor = {
    FileSystemArrayGetProperty,
    FileSystemArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
