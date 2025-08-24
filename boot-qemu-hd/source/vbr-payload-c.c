// I386 32 bits real mode
// Minimal FAT32 loader to load a binary image from FAT32 root directory.
// It won't load large files, you'll get critical errors if you try to.
// It is meant for small kernels, up to 500KB in size.

#include "../../kernel/include/String.h"
#include "../../kernel/include/I386.h"
#include "../../kernel/include/SerialPort.h"

__asm__(".code16gcc");

#define SectorSize 512
#define FileToLoad "EXOS    BIN"  // 8+3, no dot, padded
#define LoadAddress_Seg 0x2000
#define LoadAddress_Ofs 0x0000

// FAT32 special values (masked to 28 bits)
#define FAT32_MASK              0x0FFFFFFF
#define FAT32_EOC_MIN           0x0FFFFFF8
#define FAT32_BAD_CLUSTER       0x0FFFFFF7

#define GDT_SEL_CODE 0x08
#define GDT_SEL_DATA 0x10

/************************************************************************/

// GDT
typedef struct __attribute__((packed)) {
    U16 Limit;
    U32 Base;
} GDTR;

/************************************************************************/

// BIOS sector read: Drive, LBA, Count, Dest (seg:ofs packed as 0xSSSSOOOO)
extern U32 BiosReadSectors(U32 Drive, U32 Lba, U32 Count, U32 Dest);
extern void MemorySet(LPVOID Base, U32 What, U32 Size);
extern void __attribute__((noreturn)) EnterLongMode(U32 PageDirectoryPA, U32 KernelEntryVA, U32 GDTR);

static void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize);

/************************************************************************/

static inline U32 PackSegOfs(U16 Seg, U16 Ofs) {
    return ((U32)Seg << 16) | (U32)Ofs;
}

static inline U32 SegOfsToLinear(U16 Seg, U16 Ofs) {
    return ((U32)Seg << 4) | (U32)Ofs;
}

// Build seg:ofs from a linear pointer. Aligns segment down to 16 bytes.
static inline U32 MakeSegOfs(const void* Ptr) {
    U32 Lin = (U32)Ptr;
    U16 Seg = (U16)(Lin >> 4);
    U16 Ofs = (U16)(Lin & 0xF);
    return PackSegOfs(Seg, Ofs);
}

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

/************************************************************************/

const U16 COMPorts[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };

STR TempString[128];

struct Fat32BootSector BootSector;
U8 FatBuffer[SectorSize];

// NOTE: This should be high enough (e.g., up to 128 sectors) for large cluster sizes.
// For minimal BIOS calls here we keep 8 sectors worth of buffer.
U8 ClusterBuffer[SectorSize * 8];

/************************************************************************/
// Low-level I/O + A20
/************************************************************************/

static inline U8 InPortByte(U16 Port) {
    U8 Val;
    __asm__ __volatile__("inb %1, %0" : "=a"(Val) : "Nd"(Port));
    return Val;
}

static inline void OutPortByte(U16 Port, U8 Val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(Val), "Nd"(Port));
}

static void EnableA20(void) {
    // Fast A20 on port 0x92
    U8 v = InPortByte(0x92);
    if ((v & 0x02) == 0) { v |= 0x02; v &= ~0x01; OutPortByte(0x92, v); }
}

void SerialReset(U8 Which) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    /* Disable UART interrupts */
    OutPortByte(base + UART_IER, 0x00);

    /* Enable DLAB to program baud rate */
    OutPortByte(base + UART_LCR, LCR_DLAB);

    /* Set baud rate divisor (38400) */
    OutPortByte(base + UART_DLL, (U8)(BAUD_DIV_38400 & 0xFF));
    OutPortByte(base + UART_DLM, (U8)((BAUD_DIV_38400 >> 8) & 0xFF));

    /* 8N1, clear DLAB */
    OutPortByte(base + UART_LCR, LCR_8N1);

    /* Enable FIFO, clear RX/TX, set trigger level */
    OutPortByte(base + UART_FCR, (U8)(FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX | FCR_TRIG_14));

    /* Assert DTR/RTS and enable OUT2 (required for IRQ routing) */
    OutPortByte(base + UART_MCR, (U8)(MCR_DTR | MCR_RTS | MCR_OUT2));
}

void SerialOut(U8 Which, U8 Char) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    const U32 MaxSpin = 100000;
    U32 spins = 0;

    /* Wait for THR empty (LSR_THRE). Give up on timeout. */
    while (!(InPortByte(base + UART_LSR) & LSR_THRE)) {
        if (++spins >= MaxSpin) return;
    }

    OutPortByte(base + UART_THR, Char);
}

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

void Hang(void) {
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
    PrintString("[VBR] Loading and running binary OS at ");
    NumberToString(TempString, LoadAddress_Seg, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(":");
    NumberToString(TempString, LoadAddress_Ofs, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] Reading FAT32 VBR\r\n");
    if (BiosReadSectors(BootDrive, Fat32Lba, 1, MakeSegOfs(&BootSector))) {
        PrintString("[VBR] VBR read failed. Halting.\r\n");
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
        PrintString("[VBR] Invalid SectorsPerCluster = 0. Halting.\r\n");
        Hang();
    }

    if (RootCluster < 2) {
        PrintString("[VBR] Invalid RootCluster < 2. Halting.\r\n");
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

        /*
        PrintString("[VBR] Reading DIR data cluster at LBA ");
        NumberToString(TempString, Lba, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");
        */

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, MakeSegOfs(ClusterBuffer))) {
            PrintString("[VBR] DIR cluster read failed. Halting.\r\n");
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
            PrintString("[VBR] Root chain hit BAD cluster. Halting.\r\n");
            Hang();
        }

        if (Next == 0x00000000) {
            PrintString("[VBR] Root chain broken (FREE in FAT). Halting.\r\n");
            Hang();
        }

        DirCluster = Next;
    }

    if (!Found) {
        PrintString("[VBR] ERROR: EXOS.BIN not found in root directory. Halting.\r\n");
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
        /*
        PrintString("[VBR] Remaining bytes ");
        NumberToString(TempString, Remaining, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString(" | Reading data cluster ");
        NumberToString(TempString, Cluster, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");
        */

        U32 Lba = FirstDataSector + (Cluster - 2) * SectorsPerCluster;

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, PackSegOfs(DestSeg, DestOfs))) {
            PrintString("[VBR] Cluster read failed ");
            NumberToString(TempString, Cluster, 16, 0, 0, PF_SPECIAL);
            PrintString(TempString);
            PrintString(". Halting.\r\n");
            Hang();
        }

        // Simple visibility: dump first 8 bytes (2 dwords) from loaded cluster
        U32* Ptr32 = (U32*)((U32)DestSeg << 4);

        /*
        PrintString("[VBR] Cluster data (first 8 bytes): ");
        NumberToString(TempString, Ptr32[0], 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString(" ");
        NumberToString(TempString, Ptr32[1], 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");
        */

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
            PrintString("[VBR] BAD cluster in file chain. Halting.\r\n");
            Hang();
        }

        if (Next == 0x00000000) {
            PrintString("[VBR] FREE cluster in file chain (corruption). Halting.\r\n");
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
    /* Verify checksum                                                  */
    /********************************************************************/
    U8* Loaded = (U8*)(((U32)LoadAddress_Seg << 4) + (U32)LoadAddress_Ofs);

    PrintString("[VBR] Last 8 bytes of file: ");
    NumberToString(TempString, *(U32*)(Loaded + (FileSize - 8)), 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, *(U32*)(Loaded + (FileSize - 4)), 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    U32 Computed = 0;
    for (U32 Index = 0; Index < FileSize - sizeof(U32); Index++) {
        Computed += Loaded[Index];
    }

    U32 Stored = *(U32*)(Loaded + (FileSize - sizeof(U32)));

    PrintString("[VBR] Stored checksum in image : ");
    NumberToString(TempString, Stored, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    if (Computed != Stored) {
        PrintString("[VBR] Checksum mismatch. Halting. Computed : ");
        NumberToString(TempString, Computed, 16, 0, 0, PF_SPECIAL);
        PrintString(TempString);
        PrintString("\r\n");
        Hang();
    }

    /********************************************************************/
    /* Jump                                                             */
    /********************************************************************/

/*
    struct {
        unsigned short Ofs;
        unsigned short Seg;
    } __attribute__((packed)) JumpFar = {LoadAddress_Ofs, LoadAddress_Seg};

    __asm__ __volatile__("ljmp *%0" : : "m"(JumpFar));
*/

    EnterProtectedPagingAndJump(FileSize);

    Hang();
}

static LPSEGMENTDESCRIPTOR Gdt = (LPSEGMENTDESCRIPTOR)0x500;
static LPPAGEDIRECTORY PageDirectory = (LPPAGEDIRECTORY)0x1000;
static LPPAGETABLE     PageTableLow = (LPPAGETABLE)0x2000;
static LPPAGETABLE     PageTableKrn = (LPPAGETABLE)0x3000;
static GDTR Gdtr;

static void SetSegmentDescriptor(LPSEGMENTDESCRIPTOR D,
                                 U32 Base, U32 Limit,
                                 U32 Type/*0=data,1=code*/, U32 CanWrite,
                                 U32 Priv/*0*/, U32 Operand32/*1*/, U32 Gran4K/*1*/) {
    // Fill SEGMENTDESCRIPTOR bitfields (per your I386.h)
    D->Limit_00_15   = (U16)(Limit & 0xFFFF);
    D->Base_00_15    = (U16)(Base  & 0xFFFF);
    D->Base_16_23    = (U8)((Base  >> 16) & 0xFF);
    D->Accessed      = 0;
    D->CanWrite      = (CanWrite ? 1U : 0U);
    D->ConformExpand = 0;                 // non-conforming code / non expand-down data
    D->Type          = (Type ? 1U : 0U);  // 1=code, 0=data
    D->Segment       = 1;                 // code/data segment
    D->Privilege     = (Priv & 3U);
    D->Present       = 1;
    D->Limit_16_19   = (Limit >> 16) & 0xF;
    D->Available     = 0;
    D->Unused        = 0;
    D->OperandSize   = (Operand32 ? 1U : 0U);
    D->Granularity   = (Gran4K ? 1U : 0U);
    D->Base_24_31    = (U8)((Base  >> 24) & 0xFF);
}

static void BuildGdtFlat(void) {
    PrintString("[VBR] BuildGdtFlat\r\n");

    // Null
    MemorySet(Gdt + 0, 0, sizeof(SEGMENTDESCRIPTOR) * 3);

    // Code: base=0, limit=0xFFFFF with G=1 => 4GB, 32-bit
    SetSegmentDescriptor(Gdt + 1, 0x00000000, 0x000FFFFF, /*Type=*/1, /*RW=*/1,
                         /*Priv=*/0, /*32-bit=*/1, /*4K=*/1);

    // Data: base=0, limit=0xFFFFF with G=1 => 4GB, 32-bit
    SetSegmentDescriptor(Gdt + 2, 0x00000000, 0x000FFFFF, /*Type=*/0, /*RW=*/1,
                         /*Priv=*/0, /*32-bit=*/1, /*4K=*/1);

    Gdtr.Limit = (U16)(sizeof(SEGMENTDESCRIPTOR) * 3 - 1);
    Gdtr.Base  = (U32)Gdt;
}

static void ClearPdPt(void) {
    MemorySet(PageDirectory, 0, 4096);
    MemorySet(PageTableLow,  0, 4096);
    MemorySet(PageTableKrn,  0, 4096);
}

static void SetPde(LPPAGEDIRECTORY E, U32 PtPhys) {
    E->Present      = 1;
    E->ReadWrite    = 1;
    E->Privilege    = 0;
    E->WriteThrough = 0;
    E->CacheDisabled= 0;
    E->Accessed     = 0;
    E->Reserved     = 0;
    E->PageSize     = 0;                 // 0 = 4KB pages
    E->Global       = 0;
    E->User         = 0;
    E->Fixed        = 1;
    E->Address      = (PtPhys >> 12);    // top 20 bits
}

static void SetPte(LPPAGETABLE E, U32 Phys) {
    E->Present      = 1;
    E->ReadWrite    = 1;
    E->Privilege    = 0;
    E->WriteThrough = 0;
    E->CacheDisabled= 0;
    E->Accessed     = 0;
    E->Dirty        = 0;
    E->Reserved     = 0;
    E->Global       = 0;
    E->User         = 0;
    E->Fixed        = 1;
    E->Address      = (Phys >> 12);      // top 20 bits
}

static void BuildPaging(U32 KernelPhysBase, U32 KernelVirtBase, U32 MapSize) {
    PrintString("[VBR] BuildPaging (KernelPhysBase, KernelVirtBase, MapSize : ");
    NumberToString(TempString, KernelPhysBase, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, KernelVirtBase, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, MapSize, 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    ClearPdPt();

    // Identity 0..4 MB
    for (U32 i = 0; i < 1024; ++i) {
        SetPte(PageTableLow + i, i * 4096U);
    }
    SetPde(PageDirectory + 0, (U32)PageTableLow);

    // High mapping: 0xC0000000 -> KernelPhysBase .. +MapSize
    const U32 PdiK = (KernelVirtBase >> 22) & 0x3FFU;        // 768 for 0xC0000000
    SetPde(PageDirectory + PdiK, (U32)PageTableKrn);

    const U32 NumPages = (MapSize + 4095U) >> 12;
    for (U32 i = 0; i < NumPages && i < 1024U; ++i) {
        SetPte(PageTableKrn + i, KernelPhysBase + (i << 12));
    }

    SetPde(PageDirectory + 1023, (U32)PageDirectory);

    PrintString("[VBR] GDT : ");
    NumberToString(TempString, ((U32*)(Gdt))[0], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[1], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[2], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[3], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[4], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[5], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[6], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString(" ");
    NumberToString(TempString, ((U32*)(Gdt))[7], 16, 0, 0, PF_SPECIAL);
    PrintString(TempString);
    PrintString("\r\n");

    PrintString("[VBR] PDE[0], PDE[1], PDE[2], PDE[3], PDE[4], PDE[768], PDE[769] : ");
    NumberToString(TempString, ((U32*)PageDirectory)[0],   16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[1],   16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[2],   16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[3],   16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[4],   16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[768], 16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[769], 16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[770], 16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString(" ");
    NumberToString(TempString, ((U32*)PageDirectory)[1023], 16, 0, 0, PF_SPECIAL); PrintString(TempString); PrintString("\r\n");
}

void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize) {
    const U32 KernelPhysBase = SegOfsToLinear(LoadAddress_Seg, LoadAddress_Ofs);
    const U32 KernelVirtBase = 0xC0000000U;
    const U32 MapSize        = PAGE_ALIGN(FileSize + N_512KB);

    EnableA20();
    BuildGdtFlat();
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);

    // Pass kernel entry VA as a normal C value (we le-recharge dans l'asm)
    const U32 KernelEntryVA = 0xC0000000;

    for (volatile int i = 0; i < 1000000; ++i) { __asm__ __volatile__("nop"); }

    EnterLongMode((U32)(&Gdtr), (U32)PageDirectory, (U32)KernelEntryVA);

    __builtin_unreachable();
}
