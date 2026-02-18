
/************************************************************************\

    EXOS Bootloader
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


    VBR Payload FAT32 load

\************************************************************************/

#include "../include/vbr-realmode-utils.h"
#include "arch/x86-32/x86-32.h"
#include "CoreString.h"

/************************************************************************/

#define FAT32_MASK 0x0FFFFFFF
#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_BAD_CLUSTER 0x0FFFFFF7

#define MAX_SECTORS_PER_CLUSTER (USABLE_RAM_SIZE / SECTORSIZE)

/************************************************************************/

typedef struct PACKED tag_FAT32_BOOT_SECTOR {
    U8 Jump[3];
    U8 Oem[8];
    U16 BytesPerSector;
    U8 SectorsPerCluster;
    U16 ReservedSectorCount;
    U8 NumberOfFats;
    U16 RootEntryCount_NA;
    U16 TotalSectors16_NA;
    U8 Media;
    U16 SectorsPerFat16_NA;
    U16 SectorsPerTrack;
    U16 NumberOfHeads;
    U32 HiddenSectors;
    U32 NumSectors;
    U32 NumSectorsPerFat;
    U16 ExtFlags;
    U16 FsVersion;
    U32 RootCluster;
    U16 InfoSector;
    U16 BackupBootSector;
    U8 Reserved1[12];
    U8 LogicalDriveNumber;
    U8 Reserved2;
    U8 ExtendedSignature;
    U32 SerialNumber;
    U8 VolumeName[11];
    U8 FatName[8];
    U8 Code[420];
    U16 BiosMark;
} FAT32_BOOT_SECTOR;

typedef struct PACKED tag_FAT_DIR_ENTRY {
    U8 Name[11];
    U8 Attributes;
    U8 NtReserved;
    U8 CreationTimeTenth;
    U16 CreationTime;
    U16 CreationDate;
    U16 LastAccessDate;
    U16 FirstClusterHigh;
    U16 WriteTime;
    U16 WriteDate;
    U16 FirstClusterLow;
    U32 FileSize;
} FAT_DIR_ENTRY;

/************************************************************************/

static FAT32_BOOT_SECTOR BootSector;
static U8 FatBuffer[SECTORSIZE];
static U8* const ClusterBuffer = (U8*)(USABLE_RAM_START);

/************************************************************************/

static char Fat32ToUpperChar(char C) {
    if (C >= 'a' && C <= 'z') {
        return (char)(C - 'a' + 'A');
    }
    return C;
}

/************************************************************************/

static void Fat32BuildShortName(const char* KernelFile, char Out[12]) {
    for (U32 i = 0; i < 11U; ++i) {
        Out[i] = ' ';
    }
    Out[11] = '\0';

    const char* FileName = BootGetFileName(KernelFile);

    U32 BaseIndex = 0;
    const char* Ptr = FileName;
    while (*Ptr != '\0' && *Ptr != '.' && BaseIndex < 8U) {
        Out[BaseIndex++] = Fat32ToUpperChar(*Ptr++);
    }

    if (*Ptr == '.') {
        ++Ptr;
    }

    U32 ExtIndex = 0;
    while (*Ptr != '\0' && ExtIndex < 3U) {
        if (*Ptr == '.') {
            break;
        }
        Out[8U + ExtIndex] = Fat32ToUpperChar(*Ptr++);
        ++ExtIndex;
    }
}

/************************************************************************/

static int MemCmp(const void* A, const void* B, int Len) {
    const U8* X = (const U8*)A;
    const U8* Y = (const U8*)B;
    for (int I = 0; I < Len; ++I) {
        if (X[I] != Y[I]) return 1;
    }
    return 0;
}

/************************************************************************/

static U32 ReadFatEntry(U32 BootDrive, U32 FatStartSector, U32 Cluster, U32* CurrentFatSector) {
    U32 FatSector = FatStartSector + ((Cluster * 4U) / SECTORSIZE);
    U32 EntryOffset = (Cluster * 4U) % SECTORSIZE;

    if (*CurrentFatSector != FatSector) {
        if (BiosReadSectors(BootDrive, FatSector, 1, MakeSegOfs(FatBuffer))) {
            BootErrorPrint(TEXT("[VBR] FAT sector read failed\r\n"));
            Hang();
        }
        *CurrentFatSector = FatSector;
    }

    U32 Next = *(U32*)&FatBuffer[EntryOffset];
    Next &= FAT32_MASK;
    return Next;
}

/************************************************************************/

BOOL LoadKernelFat32(U32 BootDrive, U32 PartitionLba, const char* KernelFile, U32* FileSizeOut) {
    BootDebugPrint(TEXT("[VBR] Probing FAT32 filesystem\r\n"));

    if (BiosReadSectors(BootDrive, PartitionLba, 1, MakeSegOfs(&BootSector))) {
        BootErrorPrint(TEXT("[VBR] VBR read failed. Halting.\r\n"));
        Hang();
    }

    char KernelShortName[12];
    Fat32BuildShortName(KernelFile, KernelShortName);

    if (BootSector.BiosMark != 0xAA55 || BootSector.BytesPerSector != SECTORSIZE) {
        return FALSE;
    }

    U32 SectorsPerCluster = BootSector.SectorsPerCluster;
    U32 RootCluster = BootSector.RootCluster;

    if (SectorsPerCluster == 0 || RootCluster < 2) {
        BootErrorPrint(TEXT("[VBR] Invalid FAT32 parameters. Halting.\r\n"));
        Hang();
    }

    if (SectorsPerCluster > MAX_SECTORS_PER_CLUSTER) {
        BootErrorPrint(TEXT("[VBR] Max sectors per cluster exceeded. Halting.\r\n"));
        Hang();
    }

    U32 FatStartSector = PartitionLba + BootSector.ReservedSectorCount;
    U32 FatSizeSectors = BootSector.NumSectorsPerFat;
    U32 FirstDataSector = PartitionLba + BootSector.ReservedSectorCount + ((U32)BootSector.NumberOfFats * FatSizeSectors);

    BootDebugPrint(TEXT("[VBR] Scanning FAT32 root directory\r\n"));

    U32 DirCluster = RootCluster;
    U32 CurrentFatSector = 0xFFFFFFFFU;
    U32 FileCluster = 0;
    U32 FileSize = 0;
    BOOL Found = FALSE;

    while (!Found && DirCluster >= 2U && DirCluster < FAT32_EOC_MIN) {
        U32 Lba = FirstDataSector + (DirCluster - 2U) * SectorsPerCluster;
        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, MakeSegOfs(ClusterBuffer))) {
            BootErrorPrint(TEXT("[VBR] DIR cluster read failed. Halting.\r\n"));
            Hang();
        }

        for (U8* Ptr = ClusterBuffer; Ptr < ClusterBuffer + SectorsPerCluster * SECTORSIZE; Ptr += 32) {
            FAT_DIR_ENTRY* DirEntry = (FAT_DIR_ENTRY*)Ptr;

            if (DirEntry->Name[0] == 0x00) break;
            if (DirEntry->Name[0] == 0xE5) continue;
            if ((DirEntry->Attributes & 0x0F) == 0x0F) continue;

            if (MemCmp(DirEntry->Name, KernelShortName, 11) == 0) {
                FileCluster = ((U32)DirEntry->FirstClusterHigh << 16) | DirEntry->FirstClusterLow;
                FileSize = DirEntry->FileSize;
                Found = TRUE;
                break;
            }
        }

        if (!Found) {
            U32 Next = ReadFatEntry(BootDrive, FatStartSector, DirCluster, &CurrentFatSector);
            if (Next == FAT32_BAD_CLUSTER || Next == 0x00000000U) {
                BootErrorPrint(TEXT("[VBR] Corrupted FAT32 directory chain. Halting.\r\n"));
                Hang();
            }
            DirCluster = Next;
        }
    }

    if (!Found) {
        BootErrorPrint(TEXT("[VBR] Kernel %s not found on FAT32 volume.\r\n"), KernelFile);
        Hang();
    }

    BootDebugPrint(TEXT("[VBR] FAT32 kernel size %08X bytes\r\n"), FileSize);

    U32 Remaining = FileSize;
    U32 DestLinear = KERNEL_LINEAR_LOAD_ADDRESS;
    U32 Cluster = FileCluster;
    CurrentFatSector = 0xFFFFFFFFU;
    U32 ClusterBytes = (U32)SectorsPerCluster * (U32)SECTORSIZE;
    U32 MaxClusters = (FileSize + (ClusterBytes - 1U)) / ClusterBytes;
    U32 ClusterCount = 0;

    while (Remaining > 0U && Cluster >= 2U && Cluster < FAT32_EOC_MIN) {
        U32 Lba = FirstDataSector + (Cluster - 2U) * SectorsPerCluster;
        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, MakeSegOfs(ClusterBuffer))) {
            BootErrorPrint(TEXT("[VBR] Cluster read failed %08X. Halting.\r\n"), Cluster);
            Hang();
        }

        U32 BytesToCopy = (Remaining < ClusterBytes) ? Remaining : ClusterBytes;
        UnrealMemoryCopy(DestLinear, (U32)(UINT)ClusterBuffer, BytesToCopy);

        DestLinear += BytesToCopy;
        Remaining -= BytesToCopy;

        ++ClusterCount;
        if (ClusterCount > (MaxClusters + 8U)) {
            BootErrorPrint(TEXT("[VBR] FAT32 cluster chain too long. Halting.\r\n"));
            Hang();
        }

        if (Remaining == 0U) {
            break;
        }

        U32 Next = ReadFatEntry(BootDrive, FatStartSector, Cluster, &CurrentFatSector);
        if (Next == FAT32_BAD_CLUSTER || Next == 0x00000000U) {
            BootErrorPrint(TEXT("[VBR] Corrupted FAT32 file chain. Halting.\r\n"));
            Hang();
        }

        Cluster = Next;
    }

    if (FileSizeOut != NULL) {
        *FileSizeOut = FileSize;
    }

    return TRUE;
}
