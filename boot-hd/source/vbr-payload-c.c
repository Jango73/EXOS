
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

#include "../../kernel/include/arch/i386/I386.h"
#include "../../kernel/include/SerialPort.h"
#include "../../kernel/include/String.h"
#include "../include/Multiboot.h"
#include "../include/SegOfs.h"

/************************************************************************/

__asm__(".code16gcc");

#define ORIGIN 0x8000
#define STACK_SIZE 0x1000
#define USABLE_RAM_START 0x1000
#define USABLE_RAM_END (ORIGIN - STACK_SIZE)
#define USABLE_RAM_SIZE (USABLE_RAM_END - USABLE_RAM_START)

#define SECTORSIZE 512
#ifndef KERNEL_FILE
#error "KERNEL_FILE must be defined (e.g., -DFILETOLOAD=\"EXOS    BIN\")"
#endif
#define LOADADDRESS_SEG 0x2000
#define LOADADDRESS_OFS 0x0000

/************************************************************************/
// FAT32 special values (masked to 28 bits)

#define FAT32_MASK 0x0FFFFFFF
#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_BAD_CLUSTER 0x0FFFFFF7

/************************************************************************/
// I386 values

#define PAGE_DIRECTORY_ADDRESS LOW_MEMORY_PAGE_1
#define PAGE_TABLE_LOW_ADDRESS LOW_MEMORY_PAGE_2
#define PAGE_TABLE_KERNEL_ADDRESS LOW_MEMORY_PAGE_3
#define GDT_ADDRESS LOW_MEMORY_PAGE_4

#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

/************************************************************************/

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

#if DEBUG_OUTPUT == 1
#define DebugPrint(Str) WriteString(Str)
#else
#define DebugPrint(Str) ((void)0)
#endif

#define ErrorPrint(Str) WriteString(Str)

/************************************************************************/

typedef struct __attribute__((packed)) tag_FAT32_BOOT_SECTOR {
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

typedef struct __attribute__((packed)) tag_FAT_DIR_ENTRY {
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

// E820 memory map
typedef struct __attribute__((packed)) tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY;

/************************************************************************/
// Use total available memory for E80 entries and cluster

#define E820_MAX_ENTRIES 32
#define E820_SIZE (E820_MAX_ENTRIES * sizeof(E820ENTRY))
#define MAX_SECTORS_PER_CLUSTER ((USABLE_RAM_SIZE - E820_SIZE) / 512)

/************************************************************************/

static U32 E820_EntryCount = 0;
static const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};
static STR TempString[128];

FAT32_BOOT_SECTOR BootSector;
static U8 FatBuffer[SECTORSIZE];

// static E820ENTRY* const E820_Map = (E820ENTRY*)(USABLE_RAM_START);
// static U8* const ClusterBuffer = (U8*)(USABLE_RAM_START + E820_SIZE);

static E820ENTRY E820_Map[E820_MAX_ENTRIES];
static U8* const ClusterBuffer = (U8*)(USABLE_RAM_START);

// Multiboot structures - placed at a safe memory location
static multiboot_info_t MultibootInfo;
static multiboot_memory_map_t MultibootMemMap[E820_MAX_ENTRIES];
static multiboot_module_t KernelModule;
static const char BootloaderName[] = "EXOS VBR";
static const char KernelCmdLine[] = KERNEL_FILE;

/************************************************************************/
// Low-level I/O + A20

static inline U8 InPortByte(U16 Port) {
    U8 Val;
    __asm__ __volatile__("inb %1, %0" : "=a"(Val) : "Nd"(Port));
    return Val;
}

static inline void OutPortByte(U16 Port, U8 Val) { __asm__ __volatile__("outb %0, %1" ::"a"(Val), "Nd"(Port)); }

/************************************************************************/

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

/************************************************************************/

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
// Read a FAT32 entry for a given cluster with 1-sector cache.
// Parameters:
//   BootDrive, FatStartSector, Cluster, CurrentFatSector(in/out)
// Returns:
//   next cluster value (masked to 28 bits). Will Hang() on fatal errors.
//   Caller must test for EOC/BAD/FREE.

static U32 ReadFatEntry(U32 BootDrive, U32 FatStartSector, U32 Cluster, U32* CurrentFatSector) {
    U32 FatSector = FatStartSector + ((Cluster * 4) / SECTORSIZE);
    U32 EntryOffset = (Cluster * 4) % SECTORSIZE;

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
// Helper function to read byte via far pointer to avoid segment limit issues

static U8 ReadFarByte(U16 seg, U16 ofs) {
    U8 result;
    __asm__ __volatile__(
        "pushw %%ds\n\t"
        "mov %1, %%ds\n\t"
        "movb (%%si), %%al\n\t"
        "popw %%ds"
        : "=a"(result)
        : "r"(seg), "S"(ofs)
        : "memory");
    return result;
}

/************************************************************************/

static void RetrieveMemoryMap(void) {
    MemorySet((void*)E820_Map, 0, E820_SIZE);
    E820_EntryCount = BiosGetMemoryMap(MakeSegOfs(E820_Map), E820_MAX_ENTRIES);
}

/************************************************************************/

void BootMain(U32 BootDrive, U32 Fat32Lba) {
    InitDebug();

    StringPrintFormat(TempString, TEXT("[VBR] Maximum sectors per cluster : %08X\r\n"), MAX_SECTORS_PER_CLUSTER);
    DebugPrint(TempString);

    RetrieveMemoryMap();

    StringPrintFormat(
        TempString, TEXT("[VBR] Loading and running binary OS at %08X:%08X\r\n"), LOADADDRESS_SEG, LOADADDRESS_OFS);
    DebugPrint(TempString);

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

    if (SectorsPerCluster > MAX_SECTORS_PER_CLUSTER) {
        ErrorPrint(TEXT("[VBR] Max sectors per cluster exceeded. Code needs rewriting... Halting.\r\n"));
        Hang();
    }

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
    // Scan ROOT directory chain to find the file

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
        for (U8* Ptr = ClusterBuffer; Ptr < ClusterBuffer + SectorsPerCluster * SECTORSIZE; Ptr += 32) {
            FAT_DIR_ENTRY* DirEntry = (FAT_DIR_ENTRY*)Ptr;

            if (DirEntry->Name[0] == 0x00) {
                DirEnd = 1;
                break;
            }
            if (DirEntry->Name[0] == 0xE5) continue;
            if ((DirEntry->Attributes & 0x0F) == 0x0F) continue;

            if (MemCmp(DirEntry->Name, KERNEL_FILE, 11) == 0) {
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
        StringPrintFormat(TempString, TEXT("[VBR] ERROR: Kernel image (%s) not found in root directory. Halting.\r\n"), KERNEL_FILE);
        ErrorPrint(TempString);
        Hang();
    }

    StringPrintFormat(TempString, TEXT("[VBR] File size %08X bytes\r\n"), FileSize);
    DebugPrint(TempString);

    /********************************************************************/
    // Load the file by following its FAT chain

    U32 Remaining = FileSize;
    U16 DestSeg = LOADADDRESS_SEG;
    U16 DestOfs = LOADADDRESS_OFS;
    U32 Cluster = FileCluster;
    int ClusterCount = 0;
    CurrentFatSector = 0xFFFFFFFF;

    U32 ClusterBytes = (U32)SectorsPerCluster * (U32)SECTORSIZE;
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
            StringPrintFormat(TempString, TEXT("[VBR] Cluster read failed %08X. Halting.\r\n"), Cluster);
            ErrorPrint(TempString);
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
        U32 AdvanceBytes = (U32)SectorsPerCluster * (U32)SECTORSIZE;
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
    // Verify checksum

    U8* Loaded = (U8*)(((U32)LOADADDRESS_SEG << 4) + (U32)LOADADDRESS_OFS);

    // Sanity check before memory access
    if (FileSize < 8) {
        ErrorPrint(TEXT("[VBR] ERROR: FileSize too small for checksum. Halting.\r\n"));
        Hang();
    }

    // Read last 8 bytes safely using far pointers to avoid segment limit issues
    U32 BaseAddr = (U32)Loaded;
    U32 LastBytes1 = 0, LastBytes2 = 0;

    // Read bytes -8 to -5 (first U32)
    for (int i = 0; i < 4; i++) {
        U32 ByteAddr = BaseAddr + FileSize - 8 + i;
        U16 Seg = (U16)(ByteAddr >> 4);
        U16 Ofs = (U16)(ByteAddr & 0xF);
        U8 Byte = ReadFarByte(Seg, Ofs);
        LastBytes1 |= ((U32)Byte) << (i * 8);
    }

    // Read bytes -4 to -1 (second U32)
    for (int i = 0; i < 4; i++) {
        U32 ByteAddr = BaseAddr + FileSize - 4 + i;
        U16 Seg = (U16)(ByteAddr >> 4);
        U16 Ofs = (U16)(ByteAddr & 0xF);
        U8 Byte = ReadFarByte(Seg, Ofs);
        LastBytes2 |= ((U32)Byte) << (i * 8);
    }

    StringPrintFormat(TempString, TEXT("[VBR] Last 8 bytes of file: %x %x\r\n"), LastBytes1, LastBytes2);
    DebugPrint(TempString);

    U32 Computed = 0;
    for (U32 Index = 0; Index < FileSize - sizeof(U32); Index++) {
        U32 ByteAddr = BaseAddr + Index;
        U16 Seg = (U16)(ByteAddr >> 4);
        U16 Ofs = (U16)(ByteAddr & 0xF);
        Computed += ReadFarByte(Seg, Ofs);
    }

    // Read stored checksum byte-by-byte using far pointers
    U32 Stored = 0;
    for (int i = 0; i < 4; i++) {
        U32 ByteAddr = BaseAddr + FileSize - sizeof(U32) + i;
        U16 Seg = (U16)(ByteAddr >> 4);
        U16 Ofs = (U16)(ByteAddr & 0xF);
        U8 Byte = ReadFarByte(Seg, Ofs);
        Stored |= ((U32)Byte) << (i * 8);
    }

    StringPrintFormat(TempString, TEXT("[VBR] Stored checksum in image : %x\r\n"), Stored);
    DebugPrint(TempString);

    if (Computed != Stored) {
        StringPrintFormat(TempString, TEXT("[VBR] Checksum mismatch. Halting. Computed : %x\r\n"), Computed);
        ErrorPrint(TempString);
        Hang();
    }

    StringPrintFormat(TempString, TEXT("[VBR] E820 map at %x\r\n"), (U32)E820_Map);
    DebugPrint(TempString);

    StringPrintFormat(TempString, TEXT("[VBR] Cluster buffer at %x\r\n"), (U32)ClusterBuffer);
    DebugPrint(TempString);

    StringPrintFormat(TempString, TEXT("[VBR] E820 entries : %08X\r\n"), E820_EntryCount);
    DebugPrint(TempString);

    EnterProtectedPagingAndJump(FileSize);

    Hang();
}

/************************************************************************/

static LPPAGEDIRECTORY PageDirectory = (LPPAGEDIRECTORY)PAGE_DIRECTORY_ADDRESS;
static LPPAGETABLE PageTableLow = (LPPAGETABLE)PAGE_TABLE_LOW_ADDRESS;
static LPPAGETABLE PageTableKrn = (LPPAGETABLE)PAGE_TABLE_KERNEL_ADDRESS;
static GDTREGISTER Gdtr;

/************************************************************************/

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

/************************************************************************/

static void BuildGdtFlat(void) {
    // Build in a real local array so the compiler knows the bounds.
    SEGMENTDESCRIPTOR GdtBuffer[3];

    DebugPrint(TEXT("[VBR] BuildGdtFlat\r\n"));

    // Safe: compiler knows GdtBuf has exactly 3 entries
    MemorySet(GdtBuffer, 0, sizeof(GdtBuffer));

    // Code segment: base=0, limit=0xFFFFF, type=code, RW=1, gran=4K, 32-bit
    SetSegmentDescriptor(
        &GdtBuffer[1], 0x00000000, 0x000FFFFF, /*Type=*/1, /*RW=*/1,
        /*Conforming=*/0, /*Granularity4K=*/1, /*Operand32=*/1);

    // Data segment: base=0, limit=0xFFFFF, type=data, RW=1, gran=4K, 32-bit
    SetSegmentDescriptor(
        &GdtBuffer[2], 0x00000000, 0x000FFFFF, /*Type=*/0, /*RW=*/1,
        /*ExpandDown=*/0, /*Granularity4K=*/1, /*Operand32=*/1);

    // Now copy to the physical location expected by your early boot
    MemoryCopy((void*)GDT_ADDRESS, GdtBuffer, sizeof(GdtBuffer));

    Gdtr.Limit = (U16)(sizeof(GdtBuffer) - 1);
    Gdtr.Base = (U32)GDT_ADDRESS;
}

/************************************************************************/

static void ClearPdPt(void) {
    MemorySet(PageDirectory, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableLow, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableKrn, 0, PAGE_TABLE_SIZE);
}

/************************************************************************/

static void SetPageDirectoryEntry(LPPAGEDIRECTORY E, U32 PtPhys) {
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

/************************************************************************/

static void SetPageTableEntry(LPPAGETABLE E, U32 Physical) {
    BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

    E->Present = !Protected;
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
    E->Address = (Physical >> 12);  // top 20 bits
}

/************************************************************************/

static void BuildPaging(U32 KernelPhysBase, U32 KernelVirtBase, U32 MapSize) {
    ClearPdPt();

    // Identity 0..4 MB
    for (U32 i = 0; i < 1024; ++i) {
        SetPageTableEntry(PageTableLow + i, i * 4096U);
    }

    SetPageDirectoryEntry(PageDirectory + 0, (U32)PageTableLow);

    // High mapping: 0xC0000000 -> KernelPhysBase .. +MapSize
    const U32 PdiK = (KernelVirtBase >> 22) & 0x3FFU;  // 768 for 0xC0000000
    SetPageDirectoryEntry(PageDirectory + PdiK, (U32)PageTableKrn);

    const U32 NumPages = (MapSize + 4095U) >> 12;
    for (U32 i = 0; i < NumPages && i < 1024U; ++i) {
        SetPageTableEntry(PageTableKrn + i, KernelPhysBase + (i << 12));
    }

    SetPageDirectoryEntry(PageDirectory + 1023, (U32)PageDirectory);
}

/************************************************************************/

static U32 BuildMultibootInfo(U32 KernelPhysBase, U32 FileSize) {
    // Clear the multiboot info structure
    MemorySet(&MultibootInfo, 0, sizeof(multiboot_info_t));
    MemorySet(MultibootMemMap, 0, sizeof(MultibootMemMap));

    // Set up multiboot flags
    MultibootInfo.flags = MULTIBOOT_INFO_MEMORY | MULTIBOOT_INFO_MEM_MAP | MULTIBOOT_INFO_BOOT_LOADER_NAME | MULTIBOOT_INFO_MODS;

    // Convert E820 map to Multiboot memory map format
    U32 MmapLength = 0;
    for (U32 i = 0; i < E820_EntryCount && i < E820_MAX_ENTRIES; i++) {
        MultibootMemMap[i].size = sizeof(multiboot_memory_map_t) - sizeof(U32);
        MultibootMemMap[i].addr_low = E820_Map[i].Base.LO;
        MultibootMemMap[i].addr_high = E820_Map[i].Base.HI;
        MultibootMemMap[i].len_low = E820_Map[i].Size.LO;
        MultibootMemMap[i].len_high = E820_Map[i].Size.HI;

        // Convert E820 types to Multiboot types
        switch (E820_Map[i].Type) {
            case E820_AVAILABLE:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_AVAILABLE;
                break;
            case E820_RESERVED:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_RESERVED;
                break;
            case E820_ACPI:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_ACPI_RECLAIMABLE;
                break;
            case E820_NVS:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_NVS;
                break;
            case E820_UNUSABLE:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_BADRAM;
                break;
            default:
                MultibootMemMap[i].type = MULTIBOOT_MEMORY_RESERVED;
                break;
        }
        MmapLength += sizeof(multiboot_memory_map_t);
    }

    // Set memory map info
    MultibootInfo.mmap_length = MmapLength;
    MultibootInfo.mmap_addr = (U32)MultibootMemMap;

    // Compute mem_lower and mem_upper from memory map
    // mem_lower: available memory below 1MB in KB
    // mem_upper: available memory above 1MB in KB
    U32 LowerMem = 0;
    U32 UpperMem = 0;

    for (U32 i = 0; i < E820_EntryCount && i < E820_MAX_ENTRIES; i++) {
        if (MultibootMemMap[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            U32 StartLow = MultibootMemMap[i].addr_low;
            U32 StartHigh = MultibootMemMap[i].addr_high;
            U32 LengthLow = MultibootMemMap[i].len_low;
            U32 LengthHigh = MultibootMemMap[i].len_high;

            // For simplicity, only handle memory regions that fit in 32-bit space
            if (StartHigh == 0 && LengthHigh == 0) {
                U32 End = StartLow + LengthLow;

                if (StartLow < 0x100000) { // Below 1MB
                    U32 LowerEnd = (End > 0x100000) ? 0x100000 : End;
                    U32 LowerSize = LowerEnd - StartLow;
                    if (StartLow >= 0x1000) { // Exclude first 4KB
                        LowerMem += LowerSize / 1024;
                    }
                }

                if (End > 0x100000) { // Above 1MB
                    U32 UpperStart = (StartLow < 0x100000) ? 0x100000 : StartLow;
                    U32 UpperSize = End - UpperStart;
                    UpperMem += UpperSize / 1024;
                }
            } else if (StartLow >= 0x100000) {
                // Memory above 1MB - add what we can (up to 4GB)
                U32 SizeToAdd = (LengthHigh == 0) ? LengthLow : 0xFFFFFFFF - StartLow;
                UpperMem += SizeToAdd / 1024;
            }
        }
    }

    MultibootInfo.mem_lower = LowerMem;
    MultibootInfo.mem_upper = UpperMem;

    // Set bootloader name
    MultibootInfo.boot_loader_name = (U32)BootloaderName;

    // Set up kernel module
    KernelModule.mod_start = KernelPhysBase;
    KernelModule.mod_end = KernelPhysBase + FileSize;
    KernelModule.cmdline = (U32)KernelCmdLine;
    KernelModule.reserved = 0;

    // Set module information in multiboot info
    MultibootInfo.mods_count = 1;
    MultibootInfo.mods_addr = (U32)&KernelModule;

    // EXOS-specific extensions removed (cursor position now handled in Console module)

    StringPrintFormat(TempString, TEXT("[VBR] Multiboot info at %x\r\n"), (U32)&MultibootInfo);
    DebugPrint(TempString);
    StringPrintFormat(TempString, TEXT("[VBR] mem_lower=%u KB, mem_upper=%u KB\r\n"), LowerMem, UpperMem);
    DebugPrint(TempString);

    return (U32)&MultibootInfo;
}

/************************************************************************/

void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize) {
    const U32 KernelPhysBase = SegOfsToLinear(LOADADDRESS_SEG, LOADADDRESS_OFS);
    const U32 KernelVirtBase = 0xC0000000U;
    const U32 MapSize = PAGE_ALIGN(FileSize + N_512KB);

    EnableA20();
    BuildGdtFlat();
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);

    // Build the multiboot information structure
    U32 MultibootInfoPtr = BuildMultibootInfo(KernelPhysBase, FileSize);

    // Pass kernel entry VA as a normal C value
    const U32 KernelEntryVA = 0xC0000000;

    for (volatile int i = 0; i < 100000; ++i) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), (U32)PageDirectory, (U32)KernelEntryVA, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}
