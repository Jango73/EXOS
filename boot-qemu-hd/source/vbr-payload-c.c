
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


    VBR Payload main code

\************************************************************************/

// I386 32 bits real mode
// Minimal FAT32 loader to load a binary image from FAT32 root directory.
// It won't load large files, you'll get critical errors if you try to.
// It is meant for small kernels, up to 500KB in size.

#include "../../kernel/include/I386.h"
#include "../../kernel/include/SerialPort.h"
#include "../../kernel/include/String.h"

__asm__(".code16gcc");

#define SectorSize 512
#define FileToLoad "EXOS    BIN"  // 8+3, no dot, padded
#define LoadAddress_Seg 0x2000
#define LoadAddress_Ofs 0x0000

/************************************************************************/
// FAT32 special values (masked to 28 bits)

#define FAT32_MASK 0x0FFFFFFF
#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_BAD_CLUSTER 0x0FFFFFF7

/************************************************************************/
// I386 values

#define GDT_SEL_CODE 0x08
#define GDT_SEL_DATA 0x10

#define GDT_ADDRESS 0x500
#define PAGE_DIRECTORY_ADDRESS LOW_MEMORY_PAGE_1
#define PAGE_TABLE_LOW_ADDRESS LOW_MEMORY_PAGE_2
#define PAGE_TABLE_KERNEL_ADDRESS LOW_MEMORY_PAGE_3

/************************************************************************/
// Functions in vbr-payload-a.asm

// BIOS sector read: Drive, LBA, Count, Dest (seg:ofs packed as 0xSSSSOOOO)
extern U32 BiosReadSectors(U32 Drive, U32 Lba, U32 Count, U32 Dest);
extern void MemorySet(LPVOID Base, U32 What, U32 Size);
extern void MemoryCopy(LPVOID Destination, LPCVOID Source, U32 Size);
extern U32 BiosGetMemoryMap(U32 Buffer, U32 MaxEntries);
extern void __attribute__((noreturn))
StubJumpToImage(U32 GDTR, U32 PageDirectoryPA, U32 KernelEntryVA, U32 MapPtr, U32 MapCount);

/************************************************************************/
// Functions in this module

static void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize);

/************************************************************************/

#if DEBUG_OUTPUT == 2
static void InitDebug(void) { SerialReset(0); }
static void OutputChar(U8 Char) { SerialOut(0, Char); }
#else
static void InitDebug(void) {}
static void OutputChar(U8 Char) {
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Char)
        : "ah", "al");
}
#endif

static void WriteString(LPCSTR Str) {
    while (*Str) {
        OutputChar((U8)*Str++);
    }
}

#if DEBUG_OUTPUT
#define DebugPrint(Str) WriteString(Str)
#else
#define DebugPrint(Str) ((void)0)
#endif

#define ErrorPrint(Str) WriteString(Str)

static inline U32 PackSegOfs(U16 Seg, U16 Ofs) { return ((U32)Seg << 16) | (U32)Ofs; }

static inline U32 SegOfsToLinear(U16 Seg, U16 Ofs) { return ((U32)Seg << 4) | (U32)Ofs; }

// Build seg:ofs from a linear pointer. Aligns segment down to 16 bytes.
static inline U32 MakeSegOfs(const void* Ptr) {
    U32 Lin = (U32)Ptr;
    U16 Seg = (U16)(Lin >> 4);
    U16 Ofs = (U16)(Lin & 0xF);
    return PackSegOfs(Seg, Ofs);
}

/************************************************************************/

struct __attribute__((packed)) Fat32BootSector {
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
};

struct __attribute__((packed)) FatDirEntry {
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
};

/************************************************************************/

const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};

STR TempString[128];

struct Fat32BootSector BootSector;
U8 FatBuffer[SectorSize];

// NOTE: This should be high enough (e.g., up to 128 sectors) for large cluster sizes.
// For minimal BIOS calls here we keep 4 sectors worth of buffer.
// Should allocate some memory outside the payload
U8 ClusterBuffer[SectorSize * 4];

// E820 memory map
#define E820_MAX_ENTRIES 64
typedef struct __attribute__((packed)) {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY;

static E820ENTRY E820_Map[E820_MAX_ENTRIES];
static U32 E820_EntryCount = 0;

static void RetrieveMemoryMap(void) { E820_EntryCount = BiosGetMemoryMap(MakeSegOfs(E820_Map), E820_MAX_ENTRIES); }

/************************************************************************/
// Low-level I/O + A20
/************************************************************************/

static inline U8 InPortByte(U16 Port) {
    U8 Val;
    __asm__ __volatile__("inb %1, %0" : "=a"(Val) : "Nd"(Port));
    return Val;
}

static inline void OutPortByte(U16 Port, U8 Val) { __asm__ __volatile__("outb %0, %1" ::"a"(Val), "Nd"(Port)); }

static void EnableA20(void) {
    // Fast A20 on port 0x92
    U8 v = InPortByte(0x92);
    if ((v & 0x02) == 0) {
        v |= 0x02;
        v &= ~0x01;
        OutPortByte(0x92, v);
    }
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
            ErrorPrint(TEXT("[VBR] FAT sector read failed\r\n"));
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
    InitDebug();
    RetrieveMemoryMap();
    DebugPrint(TEXT("[VBR] Loading and running binary OS at "));
    NumberToString(TempString, LoadAddress_Seg, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(":"));
    NumberToString(TempString, LoadAddress_Ofs, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT("\r\n"));

    DebugPrint(TEXT("[VBR] Reading FAT32 VBR\r\n"));
    if (BiosReadSectors(BootDrive, Fat32Lba, 1, MakeSegOfs(&BootSector))) {
        ErrorPrint(TEXT("[VBR] VBR read failed. Halting.\r\n"));
        Hang();
    }

    /*
        DebugPrint(TEXT("[VBR] BIOS mark "));
        NumberToString(TempString, BootSector.BIOSMark, 16, 0, 0, PF_SPECIAL));
        DebugPrint(TempString);
        DebugPrint(TEXT("\r\n"));

        DebugPrint(TEXT("[VBR] Num sectors per FAT ");
        NumberToString(TempString, BootSector.NumSectorsPerFat, 16, 0, 0,
       PF_SPECIAL); DebugPrint(TempString); DebugPrint(TEXT("\r\n"));

        DebugPrint(TEXT("[VBR] RootCluster ");
        NumberToString(TempString, BootSector.RootCluster, 16, 0, 0,
       PF_SPECIAL); DebugPrint(TempString); DebugPrint(TEXT("\r\n"));
    */

    if (BootSector.BiosMark != 0xAA55) {
        ErrorPrint(TEXT("[VBR] BIOS mark not valid. Halting\r\n"));
        Hang();
    }

    U32 FatStartSector = Fat32Lba + BootSector.ReservedSectorCount;
    U32 FatSizeSectors = BootSector.NumSectorsPerFat;
    U32 RootCluster = BootSector.RootCluster;
    U32 SectorsPerCluster = BootSector.SectorsPerCluster;
    U32 FirstDataSector = Fat32Lba + BootSector.ReservedSectorCount + ((U32)BootSector.NumberOfFats * FatSizeSectors);

    if (SectorsPerCluster == 0) {
        ErrorPrint(TEXT("[VBR] Invalid SectorsPerCluster = 0. Halting.\r\n"));
        Hang();
    }

    if (RootCluster < 2) {
        ErrorPrint(TEXT("[VBR] Invalid RootCluster < 2. Halting.\r\n"));
        Hang();
    }

    if (SectorsPerCluster < 4) {
        DebugPrint(TEXT("[VBR] WARNING: small cluster size; expect many BIOS calls\r\n"));
    }

    /********************************************************************/
    /* Scan ROOT directory chain to find the file                       */
    /********************************************************************/
    U8 Found = 0;
    U32 FileCluster = 0, FileSize = 0;
    U32 DirCluster = RootCluster;
    U32 CurrentFatSector = 0xFFFFFFFF;
    U8 DirEnd = 0;

    DebugPrint(TEXT("[VBR] Scanning root directory chain...\r\n"));

    while (!DirEnd && DirCluster >= 2 && DirCluster < FAT32_EOC_MIN) {
        U32 Lba = FirstDataSector + (DirCluster - 2) * SectorsPerCluster;

        /*
        DebugPrint(TEXT("[VBR] Reading DIR data cluster at LBA ");
        NumberToString(TempString, Lba, 16, 0, 0, PF_SPECIAL);
        DebugPrint(TempString);
        DebugPrint(TEXT("\r\n");
        */

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, MakeSegOfs(ClusterBuffer))) {
            ErrorPrint(TEXT("[VBR] DIR cluster read failed. Halting.\r\n"));
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
                FileSize = DirEntry->FileSize;
                Found = 1;
                break;
            }
        }

        if (Found || DirEnd) break;

        // Follow root directory chain via FAT
        U32 Next = ReadFatEntry(BootDrive, FatStartSector, DirCluster, &CurrentFatSector);

        if (Next == FAT32_BAD_CLUSTER) {
            ErrorPrint(TEXT("[VBR] Root chain hit BAD cluster. Halting.\r\n"));
            Hang();
        }

        if (Next == 0x00000000) {
            ErrorPrint(TEXT("[VBR] Root chain broken (FREE in FAT). Halting.\r\n"));
            Hang();
        }

        DirCluster = Next;
    }

    if (!Found) {
        ErrorPrint(TEXT("[VBR] ERROR: EXOS.BIN not found in root directory. Halting.\r\n"));
        Hang();
    }

    DebugPrint(TEXT("[VBR] File size "));
    NumberToString(TempString, FileSize, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" bytes\r\n"));

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
        DebugPrint(TEXT("[VBR] Remaining bytes ");
        NumberToString(TempString, Remaining, 16, 0, 0, PF_SPECIAL);
        DebugPrint(TempString);
        DebugPrint(TEXT(" | Reading data cluster ");
        NumberToString(TempString, Cluster, 16, 0, 0, PF_SPECIAL);
        DebugPrint(TempString);
        DebugPrint(TEXT("\r\n");
        */

        U32 Lba = FirstDataSector + (Cluster - 2) * SectorsPerCluster;

        if (BiosReadSectors(BootDrive, Lba, SectorsPerCluster, PackSegOfs(DestSeg, DestOfs))) {
            ErrorPrint(TEXT("[VBR] Cluster read failed "));
            NumberToString(TempString, Cluster, 16, 0, 0, PF_SPECIAL);
            ErrorPrint(TempString);
            ErrorPrint(TEXT(". Halting.\r\n"));
            Hang();
        }

        // Simple visibility: dump first 8 bytes (2 dwords) from loaded cluster
        /*
        U32* Ptr32 = (U32*)((U32)DestSeg << 4);
        DebugPrint(TEXT("[VBR] Cluster data (first 8 bytes): ");
        NumberToString(TempString, Ptr32[0], 16, 0, 0, PF_SPECIAL);
        DebugPrint(TempString);
        DebugPrint(TEXT(" ");
        NumberToString(TempString, Ptr32[1], 16, 0, 0, PF_SPECIAL);
        DebugPrint(TempString);
        DebugPrint(TEXT("\r\n");
        */

        // Advance destination pointer by cluster size
        U32 AdvanceBytes = (U32)SectorsPerCluster * (U32)SectorSize;
        DestSeg += (AdvanceBytes >> 4);
        DestOfs += (U16)(AdvanceBytes & 0xF);
        if (DestOfs < (U16)(AdvanceBytes & 0xF)) {
            DestSeg += 1;
        }

        if (Remaining <= AdvanceBytes)
            Remaining = 0;
        else
            Remaining -= AdvanceBytes;

        // Get next cluster from FAT (with checks)
        U32 Next = ReadFatEntry(BootDrive, FatStartSector, Cluster, &CurrentFatSector);

        if (Next == FAT32_BAD_CLUSTER) {
            ErrorPrint(TEXT("[VBR] BAD cluster in file chain. Halting.\r\n"));
            Hang();
        }

        if (Next == 0x00000000) {
            ErrorPrint(TEXT("[VBR] FREE cluster in file chain (corruption). Halting.\r\n"));
            Hang();
        }

        Cluster = Next;
        ClusterCount++;

        if (ClusterCount > (int)(MaxClusters + 8)) {
            ErrorPrint(TEXT("[VBR] Cluster chain too long. Halting.\r\n"));
            Hang();
        }
    }

    /********************************************************************/
    /* Verify checksum                                                  */
    /********************************************************************/
    U8* Loaded = (U8*)(((U32)LoadAddress_Seg << 4) + (U32)LoadAddress_Ofs);

    DebugPrint(TEXT("[VBR] Last 8 bytes of file: "));
    NumberToString(TempString, *(U32*)(Loaded + (FileSize - 8)), 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, *(U32*)(Loaded + (FileSize - 4)), 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT("\r\n"));

    U32 Computed = 0;
    for (U32 Index = 0; Index < FileSize - sizeof(U32); Index++) {
        Computed += Loaded[Index];
    }

    U32 Stored = *(U32*)(Loaded + (FileSize - sizeof(U32)));

    DebugPrint(TEXT("[VBR] Stored checksum in image : "));
    NumberToString(TempString, Stored, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT("\r\n"));

    if (Computed != Stored) {
        ErrorPrint(TEXT("[VBR] Checksum mismatch. Halting. Computed : "));
        NumberToString(TempString, Computed, 16, 0, 0, PF_SPECIAL);
        ErrorPrint(TempString);
        ErrorPrint(TEXT("\r\n"));
        Hang();
    }

    EnterProtectedPagingAndJump(FileSize);

    Hang();
}

static LPPAGEDIRECTORY PageDirectory = (LPPAGEDIRECTORY)PAGE_DIRECTORY_ADDRESS;
static LPPAGETABLE PageTableLow = (LPPAGETABLE)PAGE_TABLE_LOW_ADDRESS;
static LPPAGETABLE PageTableKrn = (LPPAGETABLE)PAGE_TABLE_KERNEL_ADDRESS;
static GDTREGISTER Gdtr;

static void SetSegmentDescriptor(
    LPSEGMENTDESCRIPTOR D, U32 Base, U32 Limit, U32 Type /*0=data,1=code*/, U32 CanWrite, U32 Priv /*0*/,
    U32 Operand32 /*1*/, U32 Gran4K /*1*/) {
    // Fill SEGMENTDESCRIPTOR bitfields (per your I386.h)
    D->Limit_00_15 = (U16)(Limit & 0xFFFF);
    D->Base_00_15 = (U16)(Base & 0xFFFF);
    D->Base_16_23 = (U8)((Base >> 16) & 0xFF);
    D->Accessed = 0;
    D->CanWrite = (CanWrite ? 1U : 0U);
    D->ConformExpand = 0;        // non-conforming code / non expand-down data
    D->Type = (Type ? 1U : 0U);  // 1=code, 0=data
    D->Segment = 1;              // code/data segment
    D->Privilege = (Priv & 3U);
    D->Present = 1;
    D->Limit_16_19 = (Limit >> 16) & 0xF;
    D->Available = 0;
    D->Unused = 0;
    D->OperandSize = (Operand32 ? 1U : 0U);
    D->Granularity = (Gran4K ? 1U : 0U);
    D->Base_24_31 = (U8)((Base >> 24) & 0xFF);
}

static void BuildGdtFlat(void) {
    // Build in a real local array so the compiler knows the bounds.
    SEGMENTDESCRIPTOR GdtBuffer[3];

    DebugPrint(TEXT("[VBR] BuildGdtFlat\r\n"));

    /* Safe: compiler knows GdtBuf has exactly 3 entries */
    MemorySet(GdtBuffer, 0, sizeof(GdtBuffer));

    /* Code segment: base=0, limit=0xFFFFF, type=code, RW=1, gran=4K, 32-bit */
    SetSegmentDescriptor(
        &GdtBuffer[1], 0x00000000, 0x000FFFFF, /*Type=*/1, /*RW=*/1,
        /*Conforming=*/0, /*Granularity4K=*/1, /*Operand32=*/1);

    /* Data segment: base=0, limit=0xFFFFF, type=data, RW=1, gran=4K, 32-bit */
    SetSegmentDescriptor(
        &GdtBuffer[2], 0x00000000, 0x000FFFFF, /*Type=*/0, /*RW=*/1,
        /*ExpandDown=*/0, /*Granularity4K=*/1, /*Operand32=*/1);

    /* Now copy to the physical location expected by your early boot */
    MemoryCopy((void*)GDT_ADDRESS, GdtBuffer, sizeof(GdtBuffer));

    Gdtr.Limit = (U16)(sizeof(GdtBuffer) - 1);
    Gdtr.Base = (U32)GDT_ADDRESS;
}

static void ClearPdPt(void) {
    MemorySet(PageDirectory, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableLow, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableKrn, 0, PAGE_TABLE_SIZE);
}

static void SetPde(LPPAGEDIRECTORY E, U32 PtPhys) {
    E->Present = 1;
    E->ReadWrite = 1;
    E->Privilege = 0;
    E->WriteThrough = 0;
    E->CacheDisabled = 0;
    E->Accessed = 0;
    E->Reserved = 0;
    E->PageSize = 0;  // 0 = 4KB pages
    E->Global = 0;
    E->User = 0;
    E->Fixed = 1;
    E->Address = (PtPhys >> 12);  // top 20 bits
}

static void SetPte(LPPAGETABLE E, U32 Phys) {
    E->Present = 1;
    E->ReadWrite = 1;
    E->Privilege = 0;
    E->WriteThrough = 0;
    E->CacheDisabled = 0;
    E->Accessed = 0;
    E->Dirty = 0;
    E->Reserved = 0;
    E->Global = 0;
    E->User = 0;
    E->Fixed = 1;
    E->Address = (Phys >> 12);  // top 20 bits
}

static void BuildPaging(U32 KernelPhysBase, U32 KernelVirtBase, U32 MapSize) {
    DebugPrint(TEXT("[VBR] BuildPaging (KernelPhysBase, KernelVirtBase, MapSize : "));
    NumberToString(TempString, KernelPhysBase, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, KernelVirtBase, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, MapSize, 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT("\r\n"));

    ClearPdPt();

    // Identity 0..4 MB
    for (U32 i = 0; i < 1024; ++i) {
        SetPte(PageTableLow + i, i * 4096U);
    }
    SetPde(PageDirectory + 0, (U32)PageTableLow);

    // High mapping: 0xC0000000 -> KernelPhysBase .. +MapSize
    const U32 PdiK = (KernelVirtBase >> 22) & 0x3FFU;  // 768 for 0xC0000000
    SetPde(PageDirectory + PdiK, (U32)PageTableKrn);

    const U32 NumPages = (MapSize + 4095U) >> 12;
    for (U32 i = 0; i < NumPages && i < 1024U; ++i) {
        SetPte(PageTableKrn + i, KernelPhysBase + (i << 12));
    }

    SetPde(PageDirectory + 1023, (U32)PageDirectory);

    DebugPrint(TEXT("[VBR] PDE[0], PDE[1], PDE[2], PDE[3], PDE[4], PDE[768], PDE[769] : "));
    NumberToString(TempString, ((U32*)PageDirectory)[0], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[1], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[2], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[3], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[4], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[768], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[769], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[770], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT(" "));
    NumberToString(TempString, ((U32*)PageDirectory)[1023], 16, 0, 0, PF_SPECIAL);
    DebugPrint(TempString);
    DebugPrint(TEXT("\r\n"));
}

void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize) {
    const U32 KernelPhysBase = SegOfsToLinear(LoadAddress_Seg, LoadAddress_Ofs);
    const U32 KernelVirtBase = 0xC0000000U;
    const U32 MapSize = PAGE_ALIGN(FileSize + N_512KB);

    EnableA20();
    BuildGdtFlat();
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);

    // Pass kernel entry VA as a normal C value (we le-recharge dans l'asm)
    const U32 KernelEntryVA = 0xC0000000;

    for (volatile int i = 0; i < 100000; ++i) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), (U32)PageDirectory, (U32)KernelEntryVA, (U32)E820_Map, E820_EntryCount);

    __builtin_unreachable();
}
