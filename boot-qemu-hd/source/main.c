// Minimal FAT32 loader to load exos.bin from root with COM1 debug logs.

#include "../../kernel/include/String.h"

#define SectorSize 512
#define FileToLoad "EXOS    BIN"  // 8+3, no dot, padded
#define LoadAddress_Seg 0x2000
#define LoadAddress_Ofs 0x0000

// BIOS sector read: Drive, LBA, Count, Dest (seg:ofs packed as 0xSSSSOOOO)
U32 BiosReadSectors(U32 Drive, U32 Lba, U32 Count, U32 Dest);

/************************************************************************/
/* Helpers & FAT constants                                              */
/************************************************************************/

static inline U32 PackSegOfs(U16 Seg, U16 Ofs) {
    return ((U32)Seg << 16) | (U32)Ofs;
}

// Build seg:ofs from a linear pointer. Aligns segment down to 16 bytes.
static inline U32 MakeSegOfs(const void* Ptr) {
    U32 Lin = (U32)Ptr;
    U16 Seg = (U16)(Lin >> 4);
    U16 Ofs = (U16)(Lin & 0xF);
    return PackSegOfs(Seg, Ofs);
}

// FAT32 special values (masked to 28 bits)
#define FAT32_MASK              0x0FFFFFFF
#define FAT32_EOC_MIN           0x0FFFFFF8
#define FAT32_BAD_CLUSTER       0x0FFFFFF7

/************************************************************************/

struct __attribute__((packed)) Fat32BootSector {
    U8  Jump[3];
    U8  Oem[8];
    U16 BytesPerSector;
    U8  SectorsPerCluster;
    U16 ReservedSectorCount;
    U8  NumberOfFats;
    U16 RootEntryCount_NA;
    U16 TotalSectors16_NA;
    U8  Media;
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
    U8  Reserved1[12];
    U8  LogicalDriveNumber;
    U8  Reserved2;
    U8  ExtendedSignature;
    U32 SerialNumber;
    U8  VolumeName[11];
    U8  FatName[8];
    U8  Code[420];
    U16 BiosMark;
};

struct __attribute__((packed)) FatDirEntry {
    U8  Name[11];
    U8  Attributes;
    U8  NtReserved;
    U8  CreationTimeTenth;
    U16 CreationTime;
    U16 CreationDate;
    U16 LastAccessDate;
    U16 FirstClusterHigh;
    U16 WriteTime;
    U16 WriteDate;
    U16 FirstClusterLow;
    U32 FileSize;
};

struct Fat32BootSector BootSector;
U8 FatBuffer[SectorSize];

// NOTE: This should be high enough (e.g., up to 128 sectors) for large cluster sizes.
// For minimal BIOS calls here we keep 8 sectors worth of buffer.
U8 ClusterBuffer[SectorSize * 8];

/************************************************************************/

static void PrintString(const char* Str) {
    __asm__ __volatile__(
        "1:\n\t"
        "lodsb\n\t"             // AL = [ESI], ESI++
        "or    %%al, %%al\n\t"  // End of string?
        "jz    2f\n\t"
        "mov   $0x0E, %%ah\n\t" // AH = 0Eh (BIOS teletype)
        "int   $0x10\n\t"
        "jmp   1b\n\t"
        "2:\n\t"
        :
        : "S"(Str)  // ESI = s
        : "al", "ah");
}

static int MemCmp(const void* A, const void* B, int Len) {
    const U8* X = (const U8*)A;
    const U8* Y = (const U8*)B;
    for (int I = 0; I < Len; ++I) {
        if (X[I] != Y[I]) return 1;
    }
    return 0;
}

/************************************************************************/

void Hang() {
    do {
        __asm__ __volatile__(
            "1:\n\t"
            "cli\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
            :
            :
            : "memory");
    } while (0);
}

/************************************************************************/
// Read a FAT32 entry for a given cluster with 1-sector cache.
// Parameters:
//   BootDrive, FatStartSector, Cluster, CurrentFatSector(in/out)
// Returns:
//   next cluster value (masked to 28 bits). Will Hang() on fatal errors.
//   Caller must test for EOC/BAD/FREE.
static U32 ReadFatEntry(U32 BootDrive, U32 FatStartSector, U32 Cluster, U32* CurrentFatSector) {
    U32 FatSector = FatStartSector + ((Cluster * 4) / SectorSize);
    U32 EntryOffset = (Cluster * 4) % SectorSize;

    if (*CurrentFatSector != FatSector) {
        if (BiosReadSectors(BootDrive, FatSector, 1, MakeSegOfs(FatBuffer))) {
            PrintString("[VBR] FAT sector read failed\r\n");
            Hang();
        }
        *CurrentFatSector = FatSector;
    }

    U32 Next = *(U32*)&FatBuffer[EntryOffset];
    Next &= FAT32_MASK;
    return Next;
}

/************************************************************************/

void BootMain(U32 BootDrive, U32 Fat32Lba) {
    char TempString[32];

    PrintString("[VBR] Loading and running binary OS at ");
    NumberToString(TempString, LoadAddress_Seg, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(":");
    NumberToString(TempString, LoadAddress_Ofs, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Reading FAT32 VBR\r\n");
    if (BiosReadSectors(BootDrive, Fat32Lba, 1, MakeSegOfs(&BootSector))) {
        PrintString("[VBR] VBR read failed\r\n");
        Hang();
    }

    /*
        PrintString("[VBR] BIOS mark ");
        NumberToString(TempString, BootSector.BIOSMark, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");

        PrintString("[VBR] Num sectors per FAT ");
        NumberToString(TempString, BootSector.NumSectorsPerFat, 16, 0, 0,
       PF_SPECIAL); PrintString(TempString); PrintString("\r\n");

        PrintString("[VBR] RootCluster ");
        NumberToString(TempString, BootSector.RootCluster, 16, 0, 0,
       PF_SPECIAL); PrintString(TempString); PrintString("\r\n");
    */
    if (BootSector.BiosMark != 0xAA55) {
        PrintString("[VBR] BIOS mark not valid. Halting\r\n");
        Hang();
    }

    U32 FatStartSector    = Fat32Lba + BootSector.ReservedSectorCount;
    U32 FatSizeSectors    = BootSector.NumSectorsPerFat;
    U32 RootCluster       = BootSector.RootCluster;
    U32 SectorsPerCluster = BootSector.SectorsPerCluster;
    U32 FirstDataSector   = Fat32Lba + BootSector.ReservedSectorCount + ((U32)BootSector.NumberOfFats * FatSizeSectors);

    if (SectorsPerCluster == 0) {
        PrintString("[VBR] Invalid SectorsPerCluster = 0\r\n");
        Hang();
    }
    if (RootCluster < 2) {
        PrintString("[VBR] Invalid RootCluster < 2\r\n");
        Hang();
    }

    if (SectorsPerCluster < 4) {
        PrintString("[VBR] WARNING: small cluster size; expect many BIOS calls\r\n");
    }

    /********************************************************************/
    /* Scan ROOT directory chain to find the file                       */
    /********************************************************************/
    U8  Found = 0;
    U32 FileCluster = 0, FileSize = 0;
    U32 DirCluster = RootCluster;
    U32 CurrentFatSector = 0xFFFFFFFF; 
    U8  DirEnd = 0;

    PrintString("[VBR] Scanning root directory chain...\r\n");

    while (!DirEnd && DirCluster >= 2 && DirCluster < FAT32_EOC_MIN) {
        U32 Lba = FirstDataSector + (DirCluster - 2) * SectorsPerCluster;

        PrintString("[VBR] Reading DIR data cluster at LBA ");
        NumberToString(TempString, Lba, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, MakeSegOfs(ClusterBuffer))) {
            PrintString("[VBR] DIR cluster read failed\r\n");
            Hang();
        }

        // Scan 32-byte directory entries
        for (U8* Ptr = ClusterBuffer; Ptr < ClusterBuffer + SectorsPerCluster * SectorSize; Ptr += 32) {
            struct FatDirEntry* DirEntry = (struct FatDirEntry*)Ptr;

            if (DirEntry->Name[0] == 0x00) { 
                DirEnd = 1;
                break;
            }
            if (DirEntry->Name[0] == 0xE5) continue;       
            if ((DirEntry->Attributes & 0x0F) == 0x0F) continue; 

            if (MemCmp(DirEntry->Name, FileToLoad, 11) == 0) {
                FileCluster = ((U32)DirEntry->FirstClusterHigh << 16) | DirEntry->FirstClusterLow;
                FileSize    = DirEntry->FileSize;
                Found = 1;
                break;
            }
        }

        if (Found || DirEnd) break;

        // Follow root directory chain via FAT
        U32 Next = ReadFatEntry(BootDrive, FatStartSector, DirCluster, &CurrentFatSector);

        if (Next == FAT32_BAD_CLUSTER) {
            PrintString("[VBR] Root chain hit BAD cluster\r\n");
            Hang();
        }
        if (Next == 0x00000000) {
            PrintString("[VBR] Root chain broken (FREE in FAT)\r\n");
            Hang();
        }

        DirCluster = Next;
    }

    if (!Found) {
        PrintString("[VBR] ERROR: EXOS.BIN not found in root directory\r\n");
        Hang();
    }

    PrintString("[VBR] File size ");
    NumberToString(TempString, FileSize, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" bytes\r\n");

    /********************************************************************/
    /* Load the file by following its FAT chain                         */
    /********************************************************************/
    U32 Remaining = FileSize;
    U16 DestSeg = LoadAddress_Seg;
    U16 DestOfs = LoadAddress_Ofs;
    U32 Cluster = FileCluster;
    int ClusterCount = 0;
    CurrentFatSector = 0xFFFFFFFF; 

    U32 ClusterBytes = (U32)SectorsPerCluster * (U32)SectorSize;
    U32 MaxClusters = (FileSize + (ClusterBytes - 1)) / ClusterBytes;

    while (Remaining > 0 && Cluster >= 2 && Cluster < FAT32_EOC_MIN) {
        PrintString("[VBR] Remaining bytes ");
        NumberToString(TempString, Remaining, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString(" | Reading data cluster #");
        NumberToString(TempString, ClusterCount, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");

        U32 Lba = FirstDataSector + (Cluster - 2) * SectorsPerCluster;

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, PackSegOfs(DestSeg, DestOfs))) {
            PrintString("[VBR] Cluster read failed ");
            NumberToString(TempString, Cluster, 16, 0, 0, PF_SPECIAL);
            PrintString(TempString);
            PrintString("\r\n");
            Hang();
        }

        // Simple visibility: dump first 8 bytes (2 dwords) from loaded cluster
        U32* Ptr32 = (U32*)((U32)DestSeg << 4);
        PrintString("[VBR] Cluster data (first 8 bytes): ");
        NumberToString(TempString, Ptr32[0], 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString(" ");
        NumberToString(TempString, Ptr32[1], 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");

        // Advance destination pointer by cluster size
        U32 AdvanceBytes = (U32)SectorsPerCluster * (U32)SectorSize;
        DestSeg += (AdvanceBytes >> 4);
        DestOfs += (U16)(AdvanceBytes & 0xF);
        if (DestOfs < (U16)(AdvanceBytes & 0xF)) {
            DestSeg += 1;
        }

        if (Remaining <= AdvanceBytes) Remaining = 0;
        else Remaining -= AdvanceBytes;

        // Get next cluster from FAT (with checks)
        U32 Next = ReadFatEntry(BootDrive, FatStartSector, Cluster, &CurrentFatSector);

        if (Next == FAT32_BAD_CLUSTER) {
            PrintString("[VBR] BAD cluster in file chain\r\n");
            Hang();
        }
        if (Next == 0x00000000) {
            PrintString("[VBR] FREE cluster in file chain (corruption)\r\n");
            Hang();
        }

        Cluster = Next;
        ClusterCount++;

        if (ClusterCount > (int)(MaxClusters + 8)) {
            PrintString("[VBR] Cluster chain too long. Halting.\r\n");
            Hang();
        }
    }

    /********************************************************************/
    /* Verify checksum and jump                                          */
    /********************************************************************/
    U8* Loaded = (U8*)(((U32)LoadAddress_Seg << 4) + (U32)LoadAddress_Ofs);
    U32 Computed = 0;
    for (U32 I = 0; I < FileSize - 4; ++I) {
        Computed += Loaded[I];
    }
    U32 Stored = *(U32*)(Loaded + FileSize - 4);

    PrintString("[VBR] Stored checksum in image : ");
    NumberToString(TempString, Stored, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    if (Computed != Stored) {
        PrintString("[VBR] Checksum mismatch, halting : ");
        NumberToString(TempString, Computed, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");
        Hang();
    }

    PrintString("[VBR] Done, jumping to loaded image.\r\n");

    struct {
        unsigned short Ofs;
        unsigned short Seg;
    } __attribute__((packed)) JumpFar = {LoadAddress_Ofs, LoadAddress_Seg};

    __asm__ __volatile__("ljmp *%0" : : "m"(JumpFar));

    Hang();
}
