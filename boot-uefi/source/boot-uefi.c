/************************************************************************\

    EXOS UEFI Bootloader
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


    UEFI bootloader entry point

\************************************************************************/

#include "uefi/efi.h"
#include "boot-multiboot.h"
#include "vbr-realmode-utils.h"
#include "CoreString.h"

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize, U32 MultibootInfoPtr, U64 UefiImageBase, U64 UefiImageSize);

/************************************************************************/

typedef struct BOOT_UEFI_CONTEXT {
    EFI_HANDLE ImageHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_BOOT_SERVICES* BootServices;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut;
    U64 ImageBase;
    U64 ImageSize;
} BOOT_UEFI_CONTEXT;

/************************************************************************/

static const STR BootloaderNameText[] = {
    'E','X','O','S',' ','U','E','F','I',0
};
static const STR KernelFileNameText[] = {
    'e','x','o','s','.','b','i','n',0
};

/************************************************************************/

static UINT BootUefiAlignUp(UINT Value, UINT Alignment);
static U8* BootUefiAlignPointer(U8* Pointer, UINT Alignment);
static EFI_PHYSICAL_ADDRESS BootUefiPhysicalFromU32(U32 Value);
static void* BootUefiPhysicalToPointer(EFI_PHYSICAL_ADDRESS Address);
static void BootUefiOutputAscii(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Text);
static void BootUefiOutputStatus(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, EFI_STATUS Status);
static void BootUefiOutputHex32(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U32 Value);
static void BootUefiOutputHex64(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U64 Value);
static void BootUefiSerialInit(U16 PortBase);
static void BootUefiSerialWriteString(U16 PortBase, LPCSTR Text);
static EFI_STATUS BootUefiOpenRootFileSystem(BOOT_UEFI_CONTEXT* Context, EFI_FILE_PROTOCOL** RootFileOut);
static EFI_STATUS BootUefiGetFileSize(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* FilePath,
    UINT* FileSizeOut);
static EFI_STATUS BootUefiReadFile(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* FilePath,
    EFI_PHYSICAL_ADDRESS TargetAddress,
    UINT FileSize);
static EFI_STATUS BootUefiGetMemoryMap(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* MapKeyOut,
    EFI_UINTN* DescriptorSizeOut,
    EFI_UINTN* DescriptorVersionOut);
static U32 BootUefiBuildE820Map(
    const EFI_MEMORY_DESCRIPTOR* MemoryMap,
    EFI_UINTN MemoryMapSize,
    EFI_UINTN DescriptorSize,
    E820ENTRY* E820Map,
    U32 E820MaxEntries);
static U64 BootUefiShiftLeftPages(U64 Value);

/************************************************************************/

/**
 * @brief Debug logging stub for UEFI builds.
 *
 * @param Format Unused format string.
 */
void BootDebugPrint(LPCSTR Format, ...) {
    UNUSED(Format);
}

/************************************************************************/

/**
 * @brief Verbose logging stub for UEFI builds.
 *
 * @param Format Unused format string.
 */
void BootVerbosePrint(LPCSTR Format, ...) {
    UNUSED(Format);
}

/************************************************************************/

/**
 * @brief Error logging stub for UEFI builds.
 *
 * @param Format Unused format string.
 */
void BootErrorPrint(LPCSTR Format, ...) {
    UNUSED(Format);
}

/************************************************************************/

/**
 * @brief Align a value upward to the specified alignment.
 *
 * @param Value Value to align.
 * @param Alignment Alignment in bytes.
 * @return Aligned value.
 */
static UINT BootUefiAlignUp(UINT Value, UINT Alignment) {
    if (Alignment == 0u) {
        return Value;
    }

    const UINT Mask = Alignment - 1u;
    return (Value + Mask) & ~Mask;
}

/************************************************************************/

/**
 * @brief Align a pointer upward to the specified alignment.
 *
 * @param Pointer Pointer to align.
 * @param Alignment Alignment in bytes.
 * @return Aligned pointer.
 */
static U8* BootUefiAlignPointer(U8* Pointer, UINT Alignment) {
    return (U8*)(UINT)BootUefiAlignUp((UINT)(UINT)Pointer, Alignment);
}

/************************************************************************/

/**
 * @brief Build a physical address from a 32-bit value.
 *
 * @param Value 32-bit address.
 * @return UEFI physical address.
 */
static EFI_PHYSICAL_ADDRESS BootUefiPhysicalFromU32(U32 Value) {
#ifdef __EXOS_32__
    return U64_FromU32(Value);
#else
    return (EFI_PHYSICAL_ADDRESS)Value;
#endif
}

/************************************************************************/

/**
 * @brief Convert a UEFI physical address to a pointer.
 *
 * @param Address Physical address.
 * @return Pointer to the address.
 */
static void* BootUefiPhysicalToPointer(EFI_PHYSICAL_ADDRESS Address) {
#ifdef __EXOS_32__
    return (void*)(UINT)U64_Low32(Address);
#else
    return (void*)(UINT)Address;
#endif
}

/************************************************************************/

/**
 * @brief Output an ASCII string to the UEFI console.
 *
 * @param ConsoleOut Console output protocol.
 * @param Text Null-terminated ASCII string.
 */
static void BootUefiOutputAscii(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Text) {
    if (ConsoleOut == NULL || Text == NULL) {
        return;
    }

    while (*Text != '\0') {
        CHAR16 Buffer[128];
        UINT Index = 0;

        while (Text[Index] != '\0' && Index + 1u < (UINT)(sizeof(Buffer) / sizeof(Buffer[0]))) {
            Buffer[Index] = (CHAR16)(U8)Text[Index];
            Index++;
        }

        Buffer[Index] = 0;
        ConsoleOut->OutputString(ConsoleOut, Buffer);
        Text += Index;
    }
}

/************************************************************************/

/**
 * @brief Output a status code as hexadecimal.
 *
 * @param ConsoleOut Console output protocol.
 * @param Prefix Prefix string.
 * @param Status EFI status code.
 */
static void BootUefiOutputStatus(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, EFI_STATUS Status) {
    STR HexLow[9];
#if defined(__EXOS_ARCH_X86_64__)
    STR HexHigh[9];
#endif

    BootUefiOutputAscii(ConsoleOut, Prefix);
    BootUefiOutputAscii(ConsoleOut, "0x");
#if defined(__EXOS_ARCH_X86_64__)
    U32ToHexString(U64_High32((U64)Status), HexHigh);
    U32ToHexString(U64_Low32((U64)Status), HexLow);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexHigh);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexLow);
#else
    U32ToHexString((U32)Status, HexLow);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexLow);
#endif
    BootUefiOutputAscii(ConsoleOut, "\r\n");
}

/************************************************************************/

/**
 * @brief Output a 32-bit value as hexadecimal.
 *
 * @param ConsoleOut Console output protocol.
 * @param Prefix Prefix string.
 * @param Value Value to print.
 */
static void BootUefiOutputHex32(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U32 Value) {
    STR HexValue[9];

    BootUefiOutputAscii(ConsoleOut, Prefix);
    BootUefiOutputAscii(ConsoleOut, "0x");
    U32ToHexString(Value, HexValue);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexValue);
    BootUefiOutputAscii(ConsoleOut, "\r\n");
}

/************************************************************************/

/**
 * @brief Output a 64-bit value as hexadecimal.
 *
 * @param ConsoleOut Console output protocol.
 * @param Prefix Prefix string.
 * @param Value Value to print.
 */
static void BootUefiOutputHex64(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U64 Value) {
    STR HexHigh[9];
    STR HexLow[9];

    BootUefiOutputAscii(ConsoleOut, Prefix);
    BootUefiOutputAscii(ConsoleOut, "0x");
    U32ToHexString(U64_High32(Value), HexHigh);
    U32ToHexString(U64_Low32(Value), HexLow);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexHigh);
    BootUefiOutputAscii(ConsoleOut, (const char*)HexLow);
    BootUefiOutputAscii(ConsoleOut, "\r\n");
}

/************************************************************************/

/**
 * @brief Initialize a legacy serial port (COM).
 *
 * @param PortBase Base I/O port for the UART.
 */
static void BootUefiSerialInit(U16 PortBase) {
    const U16 UartData = PortBase;
    const U16 UartInterrupt = (U16)(PortBase + 0x01u);
    const U16 UartFifo = (U16)(PortBase + 0x02u);
    const U16 UartLineControl = (U16)(PortBase + 0x03u);
    const U16 UartModemControl = (U16)(PortBase + 0x04u);

    const U8 LineControlDlab = 0x80u;
    const U8 LineControl8N1 = 0x03u;
    const U8 FifoEnable = 0xC7u;
    const U8 ModemControl = 0x0Bu;

    const U16 BaudDivisor = 0x0003u;

    U8 Value = 0u;
    __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(UartInterrupt));
    __asm__ __volatile__("outb %0, %1" ::"a"(LineControlDlab), "Nd"(UartLineControl));
    Value = (U8)(BaudDivisor & 0xFFu);
    __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(UartData));
    Value = (U8)((BaudDivisor >> 8) & 0xFFu);
    __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(UartInterrupt));
    __asm__ __volatile__("outb %0, %1" ::"a"(LineControl8N1), "Nd"(UartLineControl));
    __asm__ __volatile__("outb %0, %1" ::"a"(FifoEnable), "Nd"(UartFifo));
    __asm__ __volatile__("outb %0, %1" ::"a"(ModemControl), "Nd"(UartModemControl));
}

/************************************************************************/

/**
 * @brief Write a string to a legacy serial port.
 *
 * @param PortBase Base I/O port for the UART.
 * @param Text Null-terminated string to write.
 */
static void BootUefiSerialWriteString(U16 PortBase, LPCSTR Text) {
    const U16 UartLineStatus = (U16)(PortBase + 0x05u);
    const U8 LineStatusThre = 0x20u;

    if (Text == NULL) {
        return;
    }

    while (*Text != 0u) {
        U8 Status;
        do {
            __asm__ __volatile__("inb %1, %0" : "=a"(Status) : "Nd"(UartLineStatus));
        } while ((Status & LineStatusThre) == 0u);

        U8 Value = *Text;
        __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(PortBase));
        Text++;
    }
}

/************************************************************************/

/**
 * @brief Open the root folder of the current boot device.
 *
 * @param Context UEFI context.
 * @param RootFileOut Receives the root file protocol.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiOpenRootFileSystem(BOOT_UEFI_CONTEXT* Context, EFI_FILE_PROTOCOL** RootFileOut) {
    BootUefiOutputAscii(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] Start\r\n");
    EFI_GUID LoadedImageGuid = {
        0x5B1B31A1u, 0x9562u, 0x11D2u,
        {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
    };
    EFI_GUID SimpleFileSystemGuid = {
        0x964E5B22u, 0x6459u, 0x11D2u,
        {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
    };

    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;
    EFI_STATUS Status = Context->BootServices->HandleProtocol(
        Context->ImageHandle,
        &LoadedImageGuid,
        (void**)&LoadedImage);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] HandleProtocol(LoadedImage) ", Status);
    if (Status != EFI_SUCCESS || LoadedImage == NULL) {
        return Status;
    }

    Context->ImageBase = U64_FromUINT((UINT)LoadedImage->ImageBase);
    Context->ImageSize = LoadedImage->ImageSize;
    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] ImageBase ", Context->ImageBase);
    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] ImageSize ", Context->ImageSize);

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileSystem = NULL;
    Status = Context->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle,
        &SimpleFileSystemGuid,
        (void**)&SimpleFileSystem);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] HandleProtocol(SimpleFileSystem) ", Status);
    if (Status != EFI_SUCCESS || SimpleFileSystem == NULL) {
        return Status;
    }

    Status = SimpleFileSystem->OpenVolume(SimpleFileSystem, RootFileOut);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiOpenRootFileSystem] OpenVolume ", Status);
    return Status;
}

/************************************************************************/

/**
 * @brief Get the size of a file in bytes.
 *
 * @param Context UEFI context.
 * @param RootFile Root file protocol.
 * @param FilePath File path as CHAR16.
 * @param FileSizeOut Receives the file size in bytes.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiGetFileSize(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* FilePath,
    UINT* FileSizeOut) {
    BootUefiOutputAscii(Context->ConsoleOut, "[BootUefiGetFileSize] Start\r\n");
    EFI_FILE_PROTOCOL* KernelFile = NULL;
    U64 OpenMode = U64_FromU32(0x00000001u);
    U64 Attributes = U64_0;
    EFI_STATUS Status = RootFile->Open(
        RootFile,
        &KernelFile,
        (CHAR16*)FilePath,
        OpenMode,
        Attributes);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetFileSize] Open ", Status);
    if (Status != EFI_SUCCESS || KernelFile == NULL) {
        return Status;
    }

    EFI_GUID FileInfoGuid = {
        0x9576E92u, 0x6D3Fu, 0x11D2u,
        {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
    };

    EFI_UINTN FileInfoSize = 0;
    Status = KernelFile->GetInfo(KernelFile, &FileInfoGuid, &FileInfoSize, NULL);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetFileSize] GetInfo(size) ", Status);
    BootUefiOutputHex32(Context->ConsoleOut, "[BootUefiGetFileSize] FileInfoSize ", (U32)FileInfoSize);
    if (Status != EFI_BUFFER_TOO_SMALL || FileInfoSize == 0u) {
        KernelFile->Close(KernelFile);
        return Status;
    }

    EFI_FILE_INFO* FileInfo = NULL;
    Status = Context->BootServices->AllocatePool(EfiLoaderData, FileInfoSize, (void**)&FileInfo);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetFileSize] AllocatePool ", Status);
    if (Status != EFI_SUCCESS || FileInfo == NULL) {
        KernelFile->Close(KernelFile);
        return Status;
    }

    Status = KernelFile->GetInfo(KernelFile, &FileInfoGuid, &FileInfoSize, FileInfo);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetFileSize] GetInfo(data) ", Status);
    KernelFile->Close(KernelFile);
    if (Status != EFI_SUCCESS) {
        Context->BootServices->FreePool(FileInfo);
        return Status;
    }

    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiGetFileSize] FileSize ", FileInfo->FileSize);
    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiGetFileSize] PhysicalSize ", FileInfo->PhysicalSize);
    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiGetFileSize] EntrySize ", FileInfo->Size);

    if (U64_Cmp(FileInfo->FileSize, U64_FromU32(0xFFFFFFFFu)) > 0) {
        Context->BootServices->FreePool(FileInfo);
        return EFI_BUFFER_TOO_SMALL;
    }

    *FileSizeOut = U64_ToU32_Clip(FileInfo->FileSize);
    Context->BootServices->FreePool(FileInfo);
    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read a file from the EFI system partition into memory.
 *
 * @param Context UEFI context.
 * @param RootFile Root file protocol.
 * @param FilePath File path as CHAR16.
 * @param TargetAddress Physical address to load the file.
 * @param FileSize File size in bytes.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiReadFile(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* FilePath,
    EFI_PHYSICAL_ADDRESS TargetAddress,
    UINT FileSize) {
    UNUSED(Context);

    EFI_FILE_PROTOCOL* KernelFile = NULL;
    U64 OpenMode = U64_FromU32(0x00000001u);
    U64 Attributes = U64_0;
    EFI_STATUS Status = RootFile->Open(
        RootFile,
        &KernelFile,
        (CHAR16*)FilePath,
        OpenMode,
        Attributes);
    if (Status != EFI_SUCCESS || KernelFile == NULL) {
        return Status;
    }

    EFI_UINTN BytesRead = FileSize;
    Status = KernelFile->Read(KernelFile, &BytesRead, BootUefiPhysicalToPointer(TargetAddress));
    KernelFile->Close(KernelFile);
    if (Status != EFI_SUCCESS) {
        return Status;
    }

    if (BytesRead != FileSize) {
        return EFI_BUFFER_TOO_SMALL;
    }

    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Retrieve the UEFI memory map.
 *
 * @param Context UEFI context.
 * @param MemoryMapOut Receives the allocated memory map buffer.
 * @param MemoryMapSizeOut Receives the map size.
 * @param MapKeyOut Receives the map key.
 * @param DescriptorSizeOut Receives descriptor size.
 * @param DescriptorVersionOut Receives descriptor version.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiGetMemoryMap(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* MapKeyOut,
    EFI_UINTN* DescriptorSizeOut,
    EFI_UINTN* DescriptorVersionOut) {
    BootUefiOutputAscii(Context->ConsoleOut, "[BootUefiGetMemoryMap] Start\r\n");
    EFI_UINTN MemoryMapSize = 0;
    EFI_STATUS Status = Context->BootServices->GetMemoryMap(
        &MemoryMapSize,
        NULL,
        MapKeyOut,
        DescriptorSizeOut,
        DescriptorVersionOut);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetMemoryMap] GetMemoryMap(size) ", Status);
    BootUefiOutputHex32(Context->ConsoleOut, "[BootUefiGetMemoryMap] MemoryMapSize ", (U32)MemoryMapSize);

    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    MemoryMapSize += EFI_PAGE_SIZE;

    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    Status = Context->BootServices->AllocatePool(EfiLoaderData, MemoryMapSize, (void**)&MemoryMap);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetMemoryMap] AllocatePool ", Status);
    if (Status != EFI_SUCCESS || MemoryMap == NULL) {
        return Status;
    }

    Status = Context->BootServices->GetMemoryMap(
        &MemoryMapSize,
        MemoryMap,
        MapKeyOut,
        DescriptorSizeOut,
        DescriptorVersionOut);

    if (Status != EFI_SUCCESS) {
        Context->BootServices->FreePool(MemoryMap);
        return Status;
    }

    *MemoryMapOut = MemoryMap;
    *MemoryMapSizeOut = MemoryMapSize;
    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Convert a UEFI memory type to an E820 type.
 *
 * @param MemoryType UEFI memory type.
 * @return E820 memory type.
 */
static U32 BootUefiConvertMemoryType(U32 MemoryType) {
    switch (MemoryType) {
        case EfiConventionalMemory:
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
            return E820_AVAILABLE;
        case EfiACPIReclaimMemory:
            return E820_ACPI;
        case EfiACPIMemoryNVS:
            return E820_NVS;
        case EfiUnusableMemory:
            return E820_UNUSABLE;
        default:
            return E820_RESERVED;
    }
}

/************************************************************************/

/**
 * @brief Append or merge an E820 entry.
 *
 * @param E820Map Destination E820 array.
 * @param EntryCount Current entry count.
 * @param E820MaxEntries Maximum allowed entries.
 * @param Base Physical base address.
 * @param Size Size in bytes.
 * @param Type E820 type.
 * @return Updated entry count, or 0 on overflow.
 */
static U32 BootUefiAppendE820Entry(
    E820ENTRY* E820Map,
    U32 EntryCount,
    U32 E820MaxEntries,
    U64 Base,
    U64 Size,
    U32 Type) {
    if (U64_EQUAL(Size, U64_FromU32(0u))) {
        return EntryCount;
    }

    if (EntryCount > 0u) {
        E820ENTRY* Previous = &E820Map[EntryCount - 1u];
        U64 PreviousBase = Previous->Base;
        U64 PreviousSize = Previous->Size;
        U64 PreviousEnd = U64_Add(PreviousBase, PreviousSize);

        if (Previous->Type == Type && U64_EQUAL(PreviousEnd, Base)) {
            Previous->Size = U64_Add(PreviousSize, Size);
            return EntryCount;
        }
    }

    if (EntryCount >= E820MaxEntries) {
        return 0u;
    }

    E820Map[EntryCount].Base = Base;
    E820Map[EntryCount].Size = Size;
    E820Map[EntryCount].Type = Type;
    E820Map[EntryCount].Attributes = 0u;

    return EntryCount + 1u;
}

/************************************************************************/

/**
 * @brief Shift a U64 value left by the page size (4096 bytes).
 *
 * @param Value Page count.
 * @return Byte size.
 */
static U64 BootUefiShiftLeftPages(U64 Value) {
#ifdef __EXOS_32__
    U64 Result;
    Result.HI = (Value.HI << 12) | (Value.LO >> 20);
    Result.LO = Value.LO << 12;
    return Result;
#else
    return (U64)(Value << 12);
#endif
}

/************************************************************************/

/**
 * @brief Build an E820 map from the UEFI memory descriptors.
 *
 * @param MemoryMap Memory map buffer.
 * @param MemoryMapSize Memory map size in bytes.
 * @param DescriptorSize Descriptor size in bytes.
 * @param E820Map Output E820 array.
 * @param E820MaxEntries Maximum allowed entries.
 * @return Number of E820 entries, or 0 on overflow.
 */
static U32 BootUefiBuildE820Map(
    const EFI_MEMORY_DESCRIPTOR* MemoryMap,
    EFI_UINTN MemoryMapSize,
    EFI_UINTN DescriptorSize,
    E820ENTRY* E820Map,
    U32 E820MaxEntries) {
    U32 EntryCount = 0;
    EFI_UINTN Offset = 0;

    while (Offset + DescriptorSize <= MemoryMapSize) {
        const EFI_MEMORY_DESCRIPTOR* Descriptor =
            (const EFI_MEMORY_DESCRIPTOR*)((const U8*)MemoryMap + Offset);
        U64 Base = Descriptor->PhysicalStart;
        U64 Size = BootUefiShiftLeftPages(Descriptor->NumberOfPages);
        U32 Type = BootUefiConvertMemoryType(Descriptor->Type);

        EntryCount = BootUefiAppendE820Entry(
            E820Map,
            EntryCount,
            E820MaxEntries,
            Base,
            Size,
            Type);
        if (EntryCount == 0u) {
            return 0u;
        }

        Offset += DescriptorSize;
    }

    return EntryCount;
}

/************************************************************************/

/**
 * @brief UEFI bootloader entry point.
 *
 * @param ImageHandle Current image handle.
 * @param SystemTable UEFI system table.
 * @return EFI status code (not returned on success).
 */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    BOOT_UEFI_CONTEXT Context = {
        .ImageHandle = ImageHandle,
        .SystemTable = SystemTable,
        .BootServices = SystemTable->BootServices,
        .ConsoleOut = SystemTable->ConOut
    };

    BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] Starting EXOS UEFI boot\r\n");
    BootUefiSerialInit(0x3F8u);

    EFI_FILE_PROTOCOL* RootFile = NULL;
    EFI_STATUS Status = BootUefiOpenRootFileSystem(&Context, &RootFile);
    if (Status != EFI_SUCCESS || RootFile == NULL) {
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot open root folder\r\n");
        return Status;
    }
    BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] Root folder opened\r\n");

    const CHAR16 KernelPath[] = { '\\', 'e', 'x', 'o', 's', '.', 'b', 'i', 'n', 0 };
    LPCSTR KernelFileName = KernelFileNameText;

    UINT FileSize = 0;
    Status = BootUefiGetFileSize(&Context, RootFile, KernelPath, &FileSize);
    if (Status != EFI_SUCCESS) {
        RootFile->Close(RootFile);
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot read kernel size\r\n");
        BootUefiOutputStatus(Context.ConsoleOut, "[EfiMain] Status ", Status);
        return Status;
    }
    if (FileSize == 0u) {
        RootFile->Close(RootFile);
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Kernel size is zero\r\n");
        return EFI_BUFFER_TOO_SMALL;
    }
    BootUefiOutputHex32(Context.ConsoleOut, "[EfiMain] Kernel size ", FileSize);

    UINT KernelPages = BootUefiAlignUp(FileSize, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS KernelAddress = BootUefiPhysicalFromU32(KERNEL_LINEAR_LOAD_ADDRESS);
    Status = Context.BootServices->AllocatePages(
        EFI_ALLOCATE_ADDRESS,
        EfiLoaderData,
        KernelPages,
        &KernelAddress);
    if (Status != EFI_SUCCESS) {
        RootFile->Close(RootFile);
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot reserve kernel pages\r\n");
        return Status;
    }
    BootUefiOutputHex32(Context.ConsoleOut, "[EfiMain] Kernel pages ", KernelPages);

    Status = BootUefiReadFile(&Context, RootFile, KernelPath, KernelAddress, FileSize);
    RootFile->Close(RootFile);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot read kernel file\r\n");
        return Status;
    }
    BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] Kernel loaded\r\n");

    UINT MultibootBytes =
        (UINT)sizeof(multiboot_info_t) +
        (UINT)(sizeof(multiboot_memory_map_t) * E820_MAX_ENTRIES) +
        (UINT)sizeof(multiboot_module_t) +
        (UINT)(StringLength(KernelFileName) + 1u) +
        (UINT)(StringLength(BootloaderNameText) + 1u);
    UINT MultibootPages = BootUefiAlignUp(MultibootBytes, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;

    EFI_PHYSICAL_ADDRESS MultibootBase = BootUefiPhysicalFromU32(0x001FFFFFu);
    Status = Context.BootServices->AllocatePages(
        EFI_ALLOCATE_MAX_ADDRESS,
        EfiLoaderData,
        MultibootPages,
        &MultibootBase);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot allocate Multiboot data\r\n");
        return Status;
    }

    U8* MultibootCursor = (U8*)BootUefiPhysicalToPointer(MultibootBase);
    multiboot_info_t* MultibootInfo = (multiboot_info_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_info_t);
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    multiboot_memory_map_t* MultibootMemMap = (multiboot_memory_map_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_memory_map_t) * E820_MAX_ENTRIES;
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    multiboot_module_t* KernelModule = (multiboot_module_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_module_t);
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    LPSTR BootloaderName = (LPSTR)MultibootCursor;
    StringCopy(BootloaderName, BootloaderNameText);
    MultibootCursor += StringLength(BootloaderName) + 1u;

    LPSTR KernelCommandLine = (LPSTR)MultibootCursor;
    StringCopy(KernelCommandLine, KernelFileName);

    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    EFI_UINTN MemoryMapSize = 0;
    EFI_UINTN MapKey = 0;
    EFI_UINTN DescriptorSize = 0;
    EFI_UINTN DescriptorVersion = 0;
    U32 MultibootInfoPtr = 0;

    E820ENTRY E820Map[E820_MAX_ENTRIES];
    MemorySet(E820Map, 0, sizeof(E820Map));

    for (;;) {
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] Preparing memory map\r\n");
        Status = BootUefiGetMemoryMap(
            &Context,
            &MemoryMap,
            &MemoryMapSize,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion);
        if (Status != EFI_SUCCESS) {
            BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: Cannot get memory map\r\n");
            return Status;
        }
        UNUSED(DescriptorVersion);

        Status = Context.BootServices->ExitBootServices(Context.ImageHandle, MapKey);
        if (Status == EFI_SUCCESS) {
            break;
        }

        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ExitBootServices retry\r\n");
        BootUefiOutputStatus(Context.ConsoleOut, "[EfiMain] ExitBootServices status ", Status);
        BootUefiOutputHex32(Context.ConsoleOut, "[EfiMain] ExitBootServices map key ", (U32)MapKey);

        if (MemoryMap != NULL) {
            Context.BootServices->FreePool(MemoryMap);
            MemoryMap = NULL;
        }
        MemoryMapSize = 0;
        MapKey = 0;
        DescriptorSize = 0;
        DescriptorVersion = 0;

        if (Status != EFI_INVALID_PARAMETER) {
            BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: ExitBootServices failed\r\n");
            return Status;
        }
    }

    U32 E820Count = BootUefiBuildE820Map(
        MemoryMap,
        MemoryMapSize,
        DescriptorSize,
        E820Map,
        E820_MAX_ENTRIES);
    if (E820Count == 0u) {
        BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] ERROR: E820 map overflow\r\n");
        return EFI_BUFFER_TOO_SMALL;
    }

    MultibootInfoPtr = BootBuildMultibootInfo(
        MultibootInfo,
        MultibootMemMap,
        KernelModule,
        E820Map,
        E820Count,
        KERNEL_LINEAR_LOAD_ADDRESS,
        (U32)FileSize,
        BootloaderName,
        KernelCommandLine);

    BootUefiSerialWriteString(0x3F8u, (LPCSTR)"[EfiMain] ExitBootServices ok\r\n");
    EnterProtectedPagingAndJump((U32)FileSize, MultibootInfoPtr, Context.ImageBase, Context.ImageSize);
    Hang();

    return EFI_SUCCESS;
}
