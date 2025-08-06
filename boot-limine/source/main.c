// payload_fat32_loader.c - Minimal FAT32 loader to load exos.bin from root with COM1 debug logs.

#define SectorSize         512
#define FileToLoad         "EXOS    BIN"   // 8+3, no dot, padded
#define LoadAddress        0x100000        // Kernel target addr
#define MaxSectorsPerRead  8               // For INT13h

typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned int   U32;

// Output a string to COM1 (0x3F8), polling LSR.
static void Com1Print(const char* String) {
    volatile U16 Port = 0x3F8;
    while (*String) {
        // Wait for transmit buffer empty (LSR bit 5)
        while (!(*(volatile U8*)(Port + 5) & 0x20));
        *(volatile U8*)Port = *String++;
    }
}

// Same as above, but with a newline.
static void Com1PrintLn(const char* String) {
    Com1Print(String);
    Com1Print("\r\n");
}

__attribute__((always_inline))
static inline void BiosReadSectors(U8 Drive, U32 Lba, U8 Count, void* Dest) {
    struct {
        U8  Size;
        U8  Reserved;
        U16 Count;
        U16 BufferOffset;
        U16 BufferSegment;
        U32 LbaLow;
        U32 LbaHigh;
    } __attribute__((packed)) Dap = {
        .Size = 0x10,
        .Reserved = 0,
        .Count = Count,
        .BufferOffset = ((U32)Dest) & 0xF,
        .BufferSegment = ((U32)Dest) >> 4,
        .LbaLow = Lba,
        .LbaHigh = 0,
    };
    asm volatile (
        "push %%es;"
        "movw %[seg], %%es;"
        "movb $0x42, %%ah;"
        "movb %[drv], %%dl;"
        "movl %[dap], %%esi;"
        "int $0x13;"
        "pop %%es;"
        :
        : [dap]"r"(&Dap), [drv]"r"(Drive), [seg]"r"(Dap.BufferSegment)
        : "ah", "dl", "esi"
    );
}

struct __attribute__((packed)) Fat32BootSector {
    U8  Jump[3];
    U8  Oem[8];
    U16 BytesPerSector;
    U8  SectorsPerCluster;
    U16 ReservedSectorCount;
    U8  NumberOfFats;
    U16 RootEntryCount;
    U16 TotalSectors16;
    U8  Media;
    U16 FatSize16;
    U16 SectorsPerTrack;
    U16 NumberOfHeads;
    U32 HiddenSectors;
    U32 TotalSectors32;
    U32 FatSize32;
    U16 ExtFlags;
    U16 FsVersion;
    U32 RootCluster;
    // Rest ignored
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

static int MemCmp(const void* A, const void* B, int Len) {
    const U8* X = A; const U8* Y = B;
    for (int I = 0; I < Len; ++I) if (X[I] != Y[I]) return 1;
    return 0;
}

void BootMain(U8 BootDrive) {
    Com1PrintLn("[VBR] BootMain()");
    struct Fat32BootSector Bs;
    Com1PrintLn("[VBR] Reading BootSector");
    BiosReadSectors(BootDrive, 0, 1, &Bs);

    U32 FatStartSector    = Bs.ReservedSectorCount;
    U32 FatSize          = Bs.FatSize32;
    U32 RootCluster      = Bs.RootCluster;
    U32 SectorsPerCluster= Bs.SectorsPerCluster;
    U32 FirstDataSector  = Bs.ReservedSectorCount + (Bs.NumberOfFats * FatSize);
    U32 Cluster          = RootCluster;

    U8 ClusterBuffer[SectorSize * 8];
    U8 Found = 0;
    U32 FileCluster = 0, FileSize = 0;

    Com1PrintLn("[VBR] Scanning root directory...");
    // Search root for "EXOS    BIN"
    for (;;) {
        U32 Sector = FirstDataSector + (Cluster - 2) * SectorsPerCluster;
        BiosReadSectors(BootDrive, Sector, SectorsPerCluster, ClusterBuffer);

        for (U8* P = ClusterBuffer; P < ClusterBuffer + SectorsPerCluster*SectorSize; P += 32) {
            struct FatDirEntry* D = (struct FatDirEntry*)P;
            if (D->Name[0] == 0x00) break;
            if ((D->Attr & 0x0F) == 0x0F) continue; // LFN
            if (!(MemCmp(D->Name, FileToLoad, 11))) {
                Com1PrintLn("[EXOS] exos.bin found!");
                FileCluster = ((U32)D->FstClusHi << 16) | D->FstClusLo;
                FileSize = D->FileSize;
                Found = 1; break;
            }
        }
        if (Found) break;
        Com1PrintLn("[VBR] exos.bin not in this cluster, abort");
        break; // For root dir only, no chain
    }
    if (!Found) {
        Com1PrintLn("[VBR] ERROR: exos.bin not found, hanging.");
        for(;;); // Hang
    }

    U32 Remaining = FileSize;
    U8* Dest = (U8*)LoadAddress;
    Cluster = FileCluster;
    int ClusterCount = 0;

    while (Remaining > 0 && Cluster >= 2 && Cluster < 0x0FFFFFF8) {
        Com1Print("[VBR] Reading cluster #");
        // crude decimal log
        char Num[4] = "000";
        int Tmp = Cluster;
        Num[2] = '0' + (Tmp % 10); Tmp /= 10;
        Num[1] = '0' + (Tmp % 10); Tmp /= 10;
        Num[0] = '0' + (Tmp % 10);
        Com1Print(Num);
        Com1PrintLn("");

        U32 Sector = FirstDataSector + (Cluster - 2) * SectorsPerCluster;
        BiosReadSectors(BootDrive, Sector, SectorsPerCluster, Dest);
        Dest += SectorsPerCluster * SectorSize;
        Remaining -= SectorsPerCluster * SectorSize;
        // Get next cluster
        U32 FatSector = FatStartSector + ((Cluster * 4) / SectorSize);
        U32 EntryOffset = (Cluster * 4) % SectorSize;
        U8 FatBuffer[SectorSize];
        BiosReadSectors(BootDrive, FatSector, 1, FatBuffer);
        Cluster = *(U32*)&FatBuffer[EntryOffset] & 0x0FFFFFFF;

        ClusterCount++;
        if (ClusterCount > 128) { Com1PrintLn("[VBR] Cluster chain too long, aborting."); break; }
    }
    Com1PrintLn("[VBR] Done, jumping to kernel.");
    void (*KernelEntry)(void) = (void*)LoadAddress;
    KernelEntry();
}
