// Minimal FAT32 loader to load exos.bin from root with COM1 debug logs.

#include "../../kernel/include/String.h"

#define SectorSize         512
#define FileToLoad         "EXOS    BIN"   // 8+3, no dot, padded
#define LoadAddress_Seg    0x2000
#define LoadAddress_Ofs    0x0000

U32 BiosReadSectors(U32 Drive, U32 Lba, U32 Count, void* Dest);

void PrintString(const char *s) {
    __asm__ __volatile__ (
        "1:\n\t"
        "lodsb\n\t"             // AL = [ESI], ESI++
        "or    %%al, %%al\n\t"  // test fin de chaîne
        "jz    2f\n\t"
        "mov   $0x0E, %%ah\n\t" // AH = 0Eh (BIOS teletype)
        "int   $0x10\n\t"
        "jmp   1b\n\t"
        "2:\n\t"
        :
        : "S"(s)                // ESI = s
        : "al", "ah"
    );
}

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
    U16 BIOSMark;
};

struct __attribute__((packed)) FatDirEntry {
    U8  Name[11];
    U8  Attr;
    U8  NtRes;
    U8  CrtTimeTenth;
    U16 CrtTime;
    U16 CrtDate;
    U16 LstAccDate;
    U16 FstClusHi;
    U16 WrtTime;
    U16 WrtDate;
    U16 FstClusLo;
    U32 FileSize;
};

struct Fat32BootSector BootSector;
U8 ClusterBuffer[SectorSize * 8];
U8 FatBuffer[SectorSize];

static int MemCmp(const void* A, const void* B, int Len) {
    const U8* X = A; const U8* Y = B;
    for (int I = 0; I < Len; ++I) if (X[I] != Y[I]) return 1;
    return 0;
}

void BootMain(U32 BootDrive, U32 FAT32LBA) {
    char TempString[32];

    PrintString("[VBR] Loading and running binary OS at ");
    NumberToString(TempString, LoadAddress_Seg, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString(":");
    NumberToString(TempString, LoadAddress_Ofs, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] BootSector address ");
    NumberToString(TempString, (U32)(&BootSector), 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Transmitted BootDrive ");
    NumberToString(TempString, BootDrive, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Transmitted FAT Start LBA ");
    NumberToString(TempString, FAT32LBA, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Reading FAT32 VBR\r\n");
    if (BiosReadSectors(BootDrive, FAT32LBA, 1, &BootSector)) {
        PrintString("[VBR] Reading data clusterCluster read failed\r\n");
        while(1){}; // Hang
    }

    PrintString("[VBR] BIOS mark ");
    NumberToString(TempString, BootSector.BIOSMark, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Num sectors per FAT ");
    NumberToString(TempString, BootSector.NumSectorsPerFat, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] RootCluster ");
    NumberToString(TempString, BootSector.RootCluster, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    if (BootSector.BIOSMark != 0xAA55) {
        PrintString("[VBR] BIOS mark not valid, aborting\r\n");
        while (1) {}
    }

    U32 FatStartSector    = FAT32LBA + BootSector.ReservedSectorCount;
    U32 FatSize           = BootSector.NumSectorsPerFat;
    U32 RootCluster       = BootSector.RootCluster;
    U32 SectorsPerCluster = BootSector.SectorsPerCluster;
    U32 FirstDataSector   = FAT32LBA + BootSector.ReservedSectorCount + (BootSector.NumberOfFats * FatSize);
    U32 Cluster           = RootCluster;

    PrintString("[VBR] FAT start LBA : ");
    NumberToString(TempString, FatStartSector, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] SectorsPerCluster : ");
    NumberToString(TempString, SectorsPerCluster, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    U8 Found = 0;
    U32 FileCluster = 0, FileSize = 0;

    // Search root for FileToLoad
    PrintString("[VBR] Scanning root directory...\r\n");
    for (;;) {
        U32 Sector = FirstDataSector + (Cluster - 2) * SectorsPerCluster;

        PrintString("[VBR] Reading FAT sector : ");
        NumberToString(TempString, Sector, 16, 0, 0, 0);
        PrintString(TempString);
        PrintString("\r\n");

        BiosReadSectors(BootDrive, Sector, SectorsPerCluster, ClusterBuffer);

        U32 Entry = 0;
        for (U8* P = ClusterBuffer; P < ClusterBuffer + SectorsPerCluster * SectorSize; P += 32, Entry++) {
            struct FatDirEntry* D = (struct FatDirEntry*)P;

            if (D->Name[0] == 0x00) break;
            if ((D->Attr & 0x0F) == 0x0F) continue; // LFN

            if (!(MemCmp(D->Name, FileToLoad, 11))) {
                PrintString("[VBR] Binary found in entry ");
                NumberToString(TempString, Entry, 16, 0, 0, 0);
                PrintString(TempString);
                PrintString("\r\n");

                FileCluster = ((U32)D->FstClusHi << 16) | D->FstClusLo;
                FileSize = D->FileSize;
                Found = 1; break;
            }
        }

        if (Found) break;
        PrintString("[VBR] exos.bin not in this cluster, abort\r\n");
        break; // For root dir only, no chain
    }

    if (!Found) {
        PrintString("[VBR] ERROR: exos.bin not found, hanging.\r\n");
        while(1){}; // Hang
    }

    PrintString("[VBR] File size ");
    NumberToString(TempString, FileSize, 16, 0, 0, 0);
    PrintString(TempString);
    PrintString("\r\n");

    U32 Remaining = FileSize;
    U16 Dest_Seg = LoadAddress_Seg;
    U16 Dest_Ofs = LoadAddress_Ofs;
    Cluster = FileCluster;
    int ClusterCount = 0;

    while (Remaining > 0 && Cluster >= 2 && Cluster < 0x0FFFFFF8) {
        PrintString("[VBR] Remaining data to read ");
        NumberToString(TempString, Remaining, 16, 0, 0, 0);
        PrintString(TempString);
        PrintString("\r\n");

        PrintString("[VBR] Reading data cluster ");
        NumberToString(TempString, Cluster, 16, 0, 0, 0);
        PrintString(TempString);
        PrintString("\r\n");

        U32 Sector = FirstDataSector + (Cluster - 2) * SectorsPerCluster;
        if (BiosReadSectors(BootDrive, Sector, SectorsPerCluster, (void*) (Dest_Seg << 16 | Dest_Ofs))) {
            PrintString("[VBR] Cluster read failed\r\n");
            while(1){}; // Hang
        }

        {
            U32* P32 = (U32*) (Dest_Seg << 4);
            PrintString("[VBR] Cluster data (first 16 bytes) : ");
            NumberToString(TempString, P32[0], 16, 0, 0, 0); PrintString(TempString); PrintString(" ");
            NumberToString(TempString, P32[1], 16, 0, 0, 0); PrintString(TempString); PrintString("\r\n");
        }

        Dest_Seg += (SectorsPerCluster * SectorSize) >> 4;

        U32 ClusterBytes = (U32)SectorsPerCluster * (U32)SectorSize;

        if (Remaining <= ClusterBytes) Remaining = 0; else Remaining -= ClusterBytes;

        // Get next cluster
        U32 FatSector = FatStartSector + ((Cluster * 4) / SectorSize);
        U32 EntryOffset = (Cluster * 4) % SectorSize;

        PrintString("[VBR] Reading FAT sector ");
        NumberToString(TempString, FatSector, 16, 0, 0, 0);
        PrintString(TempString);
        PrintString("\r\n");

        BiosReadSectors(BootDrive, FatSector, 1, (void*) FatBuffer);
        Cluster = *(U32*)&FatBuffer[EntryOffset] & 0x0FFFFFFF;

        ClusterCount++;
        if (ClusterCount > 128) { PrintString("[VBR] Cluster chain too long, aborting.\r\n"); break; }
    }
    PrintString("[VBR] Done, jumping to kernel.\r\n");

    void (*KernelEntry)(void) = (void*) (LoadAddress_Seg << 16 | Dest_Ofs);
    // KernelEntry();
    while(1){}; // Hang
}
