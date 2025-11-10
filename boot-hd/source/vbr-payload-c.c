
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

// i386 32 bits real mode payload entry point

#include "../include/vbr-multiboot.h"
#include "../include/vbr-realmode-utils.h"
#include "../include/vbr-payload-shared.h"
#include "arch/i386/i386.h"
#include "SerialPort.h"
#include "CoreString.h"

/************************************************************************/

__asm__(".code16gcc");

#ifndef KERNEL_FILE
#error "KERNEL_FILE must be defined (e.g., -DKERNEL_FILE=\"exos.bin\")"
#endif

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize);
BOOL LoadKernelFat32(U32 BootDrive, U32 PartitionLba, const char* KernelFile, U32* FileSizeOut);
BOOL LoadKernelExt2(U32 BootDrive, U32 PartitionLba, const char* KernelName, U32* FileSizeOut);

/************************************************************************/

static void InitDebug(void);
static void OutputChar(U8 Char);
static void WriteString(LPCSTR Str);

STR TempString[128];
static const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};

/************************************************************************/

static void InitDebug(void) {
#if DEBUG_OUTPUT == 2
    SerialReset(0);
#endif
}

/************************************************************************/

static void OutputChar(U8 Char) {
#if DEBUG_OUTPUT == 2
    SerialOut(0, Char);
#else
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Char)
        : "ah", "al");
#endif
}

/************************************************************************/

static void WriteString(LPCSTR Str) {
    while (*Str) {
        OutputChar((U8)*Str++);
    }
}

/************************************************************************/

void BootDebugPrint(LPCSTR Format, ...) {
#if DEBUG_OUTPUT == 0
    UNUSED(Format);
#else
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
#endif
}

/************************************************************************/

void BootVerbosePrint(LPCSTR Format, ...) {
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
}

/************************************************************************/

void BootErrorPrint(LPCSTR Format, ...) {
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
}

/************************************************************************/

const char* BootGetFileName(const char* Path) {
    if (Path == NULL) {
        return "";
    }

    const char* Result = Path;
    for (const char* Ptr = Path; *Ptr != '\0'; ++Ptr) {
        if (*Ptr == '/' || *Ptr == '\\') {
            Result = Ptr + 1;
        }
    }

    return Result;
}

/************************************************************************/

static char ToLowerChar(char C) {
    if (C >= 'A' && C <= 'Z') {
        return (char)(C - 'A' + 'a');
    }
    return C;
}

/************************************************************************/

static void BuildKernelExt2Name(char* Out, U32 OutSize) {
    if (Out == NULL || OutSize == 0U) {
        return;
    }

    const char* FileName = BootGetFileName(KERNEL_FILE);
    U32 Pos = 0;

    for (const char* Ptr = FileName; *Ptr != '\0' && Pos + 1U < OutSize; ++Ptr) {
        Out[Pos++] = ToLowerChar(*Ptr);
    }

    Out[Pos] = '\0';
}

/************************************************************************/

static void VerifyKernelImage(U32 FileSize) {
    if (FileSize < 8U) {
        BootErrorPrint(TEXT("[VBR] ERROR: FileSize too small for checksum. Halting.\r\n"));
        Hang();
    }

    const U8* const FileStart = (const U8*)KERNEL_LINEAR_LOAD_ADDRESS;
    const U8* const ChecksumPtr = FileStart + FileSize - sizeof(U32);

    BootDebugPrint(
        TEXT("[VBR] VerifyKernelImage scanning %u data bytes\r\n"),
        FileSize - (U32)sizeof(U32));

    U32 LastBytes1 = 0;
    U32 LastBytes2 = 0;

    for (int Index = 0; Index < 4; ++Index) {
        LastBytes1 |= ((U32)FileStart[FileSize - 8U + (U32)Index]) << (Index * 8);
        LastBytes2 |= ((U32)FileStart[FileSize - 4U + (U32)Index]) << (Index * 8);
    }

    BootDebugPrint(TEXT("[VBR] Last 8 bytes of file: %x %x\r\n"), LastBytes1, LastBytes2);

    U32 Computed = 0;
    for (U32 Index = 0; Index < FileSize - sizeof(U32); ++Index) {
        Computed += FileStart[Index];
    }

    U32 Stored = 0;
    for (int Index = 0; Index < 4; ++Index) {
        Stored |= ((U32)ChecksumPtr[Index]) << (Index * 8);
    }

    BootDebugPrint(TEXT("[VBR] Stored checksum in image : %x\r\n"), Stored);

    if (Computed == Stored) {
        BootDebugPrint(
            TEXT("[VBR] Image checksum OK. Stored : %x vs computed : %x\r\n"),
            Stored,
            Computed);
    } else {
        BootErrorPrint(
            TEXT("[VBR] Checksum mismatch. Halting. Stored : %x vs computed : %x\r\n"),
            Stored,
            Computed);
        Hang();
    }
}

/************************************************************************/
// E820 memory map buffers shared with architecture specific code
/************************************************************************/

U32 E820_EntryCount = 0;
E820ENTRY E820_Map[E820_MAX_ENTRIES];

// Multiboot structures - placed at a safe memory location
multiboot_info_t MultibootInfo;
multiboot_memory_map_t MultibootMemMap[E820_MAX_ENTRIES];
multiboot_module_t KernelModule;
const char BootloaderName[] = "EXOS VBR";
const char KernelCmdLine[] = KERNEL_FILE;

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

static void RetrieveMemoryMap(void) {
    MemorySet((void*)E820_Map, 0, E820_SIZE);
    E820_EntryCount = BiosGetMemoryMap(MakeSegOfs(E820_Map), E820_MAX_ENTRIES);

    BootDebugPrint(TEXT("[VBR] E820 map at %x\r\n"), (U32)E820_Map);
    BootDebugPrint(TEXT("[VBR] E820 entry count : %d\r\n"), E820_EntryCount);
}

/************************************************************************/

U32 BuildMultibootInfo(U32 KernelPhysBase, U32 FileSize) {
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

    BootDebugPrint(TEXT("[VBR] Multiboot info at %x\r\n"), (U32)&MultibootInfo);
    BootDebugPrint(TEXT("[VBR] mem_lower=%u KB, mem_upper=%u KB\r\n"), LowerMem, UpperMem);

    return (U32)&MultibootInfo;
}

/************************************************************************/

void BootMain(U32 BootDrive, U32 PartitionLba) {
    InitDebug();

    RetrieveMemoryMap();

    BootDebugPrint(
        TEXT("[VBR] Loading and running binary OS at %08X\r\n"),
        KERNEL_LINEAR_LOAD_ADDRESS);

    char Ext2KernelName[32];
    BuildKernelExt2Name(Ext2KernelName, sizeof(Ext2KernelName));

    U32 FileSize = 0;
    const char* LoadedFs = NULL;

    if (LoadKernelFat32(BootDrive, PartitionLba, KERNEL_FILE, &FileSize)) {
        LoadedFs = "FAT32";
    } else if (LoadKernelExt2(BootDrive, PartitionLba, Ext2KernelName, &FileSize)) {
        LoadedFs = "EXT2";
    } else {
        BootErrorPrint(TEXT("[VBR] Unsupported filesystem detected. Halting.\r\n"));
        Hang();
    }

    BootDebugPrint(TEXT("[VBR] Kernel loaded via %s\r\n"), LoadedFs);

    VerifyKernelImage(FileSize);

    BootDebugPrint(TEXT("[VBR] Calling architecture specific boot code\r\n"));

    EnterProtectedPagingAndJump(FileSize);

    Hang();
}
