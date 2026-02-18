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
#include "uefi/uefi-log-udp.h"
#include "boot-multiboot.h"
#include "boot-reservation.h"
#include "vbr-realmode-utils.h"
#include "CoreString.h"

/************************************************************************/

#ifndef BOOT_STAGE_MARKERS
#define BOOT_STAGE_MARKERS 0
#endif

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize, U32 MultibootInfoPtr, U64 UefiImageBase, U64 UefiImageSize);

/************************************************************************/

typedef struct BOOT_UEFI_CONTEXT {
    EFI_HANDLE ImageHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_BOOT_SERVICES* BootServices;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput;
    BOOL BootServicesExited;
    U64 ImageBase;
    U64 ImageSize;
} BOOT_UEFI_CONTEXT;

/************************************************************************/

typedef struct BOOT_UEFI_MULTIBOOT_LAYOUT {
    multiboot_info_t* MultibootInfo;
    multiboot_memory_map_t* MultibootMemoryMap;
    multiboot_module_t* KernelModule;
    LPSTR BootloaderName;
    LPSTR KernelCommandLine;
} BOOT_UEFI_MULTIBOOT_LAYOUT;

/************************************************************************/

static const STR BootloaderNameText[] = {
    'E','X','O','S',' ','U','E','F','I',0
};
static const STR KernelFileNameText[] = {
    'e','x','o','s','.','b','i','n',0
};

/************************************************************************/

enum {
    BOOT_UEFI_STAGE_BOOT_START = 0u,
    BOOT_UEFI_STAGE_DEBUG_TRANSPORT_READY = 1u,
    BOOT_UEFI_STAGE_ROOT_FOLDER_OPENED = 2u,
    BOOT_UEFI_STAGE_KERNEL_LOADED = 3u,
    BOOT_UEFI_STAGE_MULTIBOOT_ALLOCATED = 4u,
    BOOT_UEFI_STAGE_FRAMEBUFFER_QUERIED = 5u,
    BOOT_UEFI_STAGE_RSDP_CAPTURED = 6u,
    BOOT_UEFI_STAGE_EXIT_BOOT_BEGIN = 7u,
    BOOT_UEFI_STAGE_EXIT_BOOT_DONE = 8u,
    BOOT_UEFI_STAGE_E820_READY = 9u,
    BOOT_UEFI_STAGE_MULTIBOOT_READY = 10u,
    BOOT_UEFI_STAGE_UDP_LOCATE = 11u,
    BOOT_UEFI_STAGE_UDP_START = 12u,
    BOOT_UEFI_STAGE_UDP_INITIALIZE = 13u,
    BOOT_UEFI_STAGE_UDP_ENABLED = 14u
};

/************************************************************************/

// Used by the x86-64 UEFI jump stub to fetch parameters reliably.
U32 UefiStubMultibootInfoPtr = 0u;
U32 UefiStubMultibootMagic = 0u;
U32 UefiStubTestOnly = 0u;
U32 UefiStubKernelPhysicalBase = 0u;

/************************************************************************/

static UINT BootUefiAlignUp(UINT Value, UINT Alignment);
static U8* BootUefiAlignPointer(U8* Pointer, UINT Alignment);
static EFI_PHYSICAL_ADDRESS BootUefiPhysicalFromU32(U32 Value);
static U64 BootUefiPhysicalToU64(EFI_PHYSICAL_ADDRESS Address);
static void* BootUefiPhysicalToPointer(EFI_PHYSICAL_ADDRESS Address);
static void BootUefiOutputAscii(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Text);
static void BootUefiOutputStatus(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, EFI_STATUS Status);
static void BootUefiOutputHex32(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U32 Value);
static void BootUefiOutputHex64(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut, const char* Prefix, U64 Value);
static void BootUefiSerialInit(U16 PortBase);
static void BootUefiSerialWriteString(U16 PortBase, LPCSTR Text);
static void BootUefiDebugTransportInit(BOOT_UEFI_CONTEXT* Context);
static void BootUefiDebugTransportWrite(LPCSTR Text);
static void BootUefiDebugTransportNotifyExitBootServices(void);
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
static EFI_STATUS BootUefiGetMemoryMapSilent(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* MapKeyOut,
    EFI_UINTN* DescriptorSizeOut,
    EFI_UINTN* DescriptorVersionOut);
typedef EFI_STATUS (*EFI_STALL_FN)(EFI_UINTN);
typedef EFI_STATUS (*EFI_SET_WATCHDOG_TIMER_FN)(EFI_UINTN, EFI_UINTN, EFI_UINTN, CHAR16*);
static void BootUefiDisableWatchdog(BOOT_UEFI_CONTEXT* Context);
static void NORETURN BootUefiHalt(BOOT_UEFI_CONTEXT* Context, const char* Reason);
static U32 BootUefiBuildE820Map(
    const EFI_MEMORY_DESCRIPTOR* MemoryMap,
    EFI_UINTN MemoryMapSize,
    EFI_UINTN DescriptorSize,
    E820ENTRY* E820Map,
    U32 E820MaxEntries);
static U64 BootUefiShiftLeftPages(U64 Value);
static BOOL BootUefiGetFramebufferInfo(BOOT_UEFI_CONTEXT* Context, BOOT_FRAMEBUFFER_INFO* FramebufferInfo);
static U32 BootUefiMaskPosition(U32 Mask);
static U32 BootUefiMaskSize(U32 Mask);
static U32 BootUefiScaleColorToMask(U8 Value, U32 MaskSize);
static EFI_GRAPHICS_OUTPUT_PROTOCOL* BootUefiGetGraphicsOutput(BOOT_UEFI_CONTEXT* Context);
static U32 BootUefiComposePixelColor(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info, U8 Red, U8 Green, U8 Blue);
static void BootUefiMarkStage(BOOT_UEFI_CONTEXT* Context, U32 StageIndex, U8 Red, U8 Green, U8 Blue);
static void NORETURN BootUefiHaltNoServices(void);
static EFI_STATUS BootUefiOpenRootFolder(BOOT_UEFI_CONTEXT* Context, EFI_FILE_PROTOCOL** RootFileOut);
static EFI_STATUS BootUefiLoadKernelImage(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* KernelPath,
    UINT* FileSizeOut,
    U32* KernelPhysicalBaseOut,
    U32* KernelReservedBytesOut);
static EFI_STATUS BootUefiAllocateMultibootData(
    BOOT_UEFI_CONTEXT* Context,
    LPCSTR KernelFileName,
    BOOT_UEFI_MULTIBOOT_LAYOUT* LayoutOut);
static U32 BootUefiGetRsdpPhysicalLow(BOOT_UEFI_CONTEXT* Context);
static EFI_STATUS BootUefiExitBootServicesWithRetry(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* DescriptorSizeOut);
static void NORETURN BootUefiEnterKernel(
    U32 FileSize,
    U32 MultibootInfoPtr,
    U32 KernelPhysicalBase,
    U64 UefiImageBase,
    U64 UefiImageSize);

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
 * @brief Convert a UEFI physical address to a 64-bit value.
 *
 * @param Address Physical address.
 * @return 64-bit address value.
 */
static U64 BootUefiPhysicalToU64(EFI_PHYSICAL_ADDRESS Address) {
#ifdef __EXOS_32__
    return Address;
#else
    return (U64)Address;
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
 * @brief Waits for a key press on the UEFI console input.
 *
 * @param Context UEFI context.
 */
/**
 * @brief Disable the UEFI watchdog timer to avoid automatic resets.
 *
 * @param Context UEFI context.
 */
static void BootUefiDisableWatchdog(BOOT_UEFI_CONTEXT* Context) {
    if (Context == NULL || Context->BootServices == NULL || Context->BootServices->SetWatchdogTimer == NULL) {
        return;
    }

    EFI_SET_WATCHDOG_TIMER_FN SetWatchdogTimer =
        (EFI_SET_WATCHDOG_TIMER_FN)Context->BootServices->SetWatchdogTimer;
    SetWatchdogTimer(0, 0, 0, NULL);
}

/************************************************************************/

/**
 * @brief Halt execution to keep logs visible.
 *
 * @param Context UEFI context.
 * @param Reason Halt reason string.
 */
static void NORETURN BootUefiHalt(BOOT_UEFI_CONTEXT* Context, const char* Reason) {
    if (Context != NULL && Context->ConsoleOut != NULL && Reason != NULL) {
        BootUefiOutputAscii(Context->ConsoleOut, "[BootUefiHalt] ");
        BootUefiOutputAscii(Context->ConsoleOut, Reason);
        BootUefiOutputAscii(Context->ConsoleOut, "\r\n");
    }

    if (Context != NULL && Context->BootServices != NULL && Context->BootServices->Stall != NULL) {
        EFI_STALL_FN Stall = (EFI_STALL_FN)Context->BootServices->Stall;
        for (;;) {
            Stall(2000000);
        }
    }

    for (;;) {
        __asm__ __volatile__("hlt");
    }
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
 * @brief Initialize debug transport (serial or UDP).
 *
 * @param Context UEFI context.
 */
static void BootUefiDebugTransportInit(BOOT_UEFI_CONTEXT* Context) {
#if UEFI_LOG_USE_UDP == 1
    if (Context == NULL) {
        return;
    }

    BootUefiUdpLogInitialize(Context->BootServices);
    U32 InitFlags = BootUefiUdpLogGetInitFlags();
    BootUefiMarkStage(
        Context,
        BOOT_UEFI_STAGE_UDP_LOCATE,
        (InitFlags & UEFI_UDP_INIT_FLAG_LOCATE_OK) != 0u ? 0u : 255u,
        (InitFlags & UEFI_UDP_INIT_FLAG_LOCATE_OK) != 0u ? 200u : 0u,
        0u);
    BootUefiMarkStage(
        Context,
        BOOT_UEFI_STAGE_UDP_START,
        (InitFlags & UEFI_UDP_INIT_FLAG_START_OK) != 0u ? 0u : 255u,
        (InitFlags & UEFI_UDP_INIT_FLAG_START_OK) != 0u ? 200u : 0u,
        0u);
    BootUefiMarkStage(
        Context,
        BOOT_UEFI_STAGE_UDP_INITIALIZE,
        (InitFlags & UEFI_UDP_INIT_FLAG_INITIALIZE_OK) != 0u ? 0u : 255u,
        (InitFlags & UEFI_UDP_INIT_FLAG_INITIALIZE_OK) != 0u ? 200u : 0u,
        0u);
    BootUefiMarkStage(
        Context,
        BOOT_UEFI_STAGE_UDP_ENABLED,
        (InitFlags & UEFI_UDP_INIT_FLAG_ENABLED) != 0u ? 0u : 255u,
        (InitFlags & UEFI_UDP_INIT_FLAG_ENABLED) != 0u ? 200u : 0u,
        0u);
#else
    UNUSED(Context);
    BootUefiSerialInit(0x3F8u);
#endif
}

/************************************************************************/

/**
 * @brief Write text to the selected debug transport.
 *
 * @param Text Text to output.
 */
static void BootUefiDebugTransportWrite(LPCSTR Text) {
#if UEFI_LOG_USE_UDP == 1
    BootUefiUdpLogWrite(Text);
#else
    BootUefiSerialWriteString(0x3F8u, Text);
#endif
}

/************************************************************************/

/**
 * @brief Notify transport that boot services are no longer available.
 */
static void BootUefiDebugTransportNotifyExitBootServices(void) {
#if UEFI_LOG_USE_UDP == 1
    BootUefiUdpLogNotifyExitBootServices();
#endif
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

    #ifdef __EXOS_32__
    Context->ImageBase = U64_FromU32((U32)(UINT)LoadedImage->ImageBase);
    #else
    Context->ImageBase = BootUefiPhysicalToU64((EFI_PHYSICAL_ADDRESS)LoadedImage->ImageBase);
    #endif
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
 * @brief Compare two EFI GUID values.
 *
 * @param Left First GUID.
 * @param Right Second GUID.
 * @return TRUE if equal, FALSE otherwise.
 */
static BOOL BootUefiGuidEquals(const EFI_GUID* Left, const EFI_GUID* Right) {
    if (Left == NULL || Right == NULL) {
        return FALSE;
    }

    if (Left->Data1 != Right->Data1 || Left->Data2 != Right->Data2 || Left->Data3 != Right->Data3) {
        return FALSE;
    }

    for (U32 Index = 0; Index < 8u; Index++) {
        if (Left->Data4[Index] != Right->Data4[Index]) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Locate the ACPI RSDP from the UEFI configuration table.
 *
 * @param Context UEFI context.
 * @return Physical address of the RSDP, or 0 on failure.
 */
static EFI_PHYSICAL_ADDRESS BootUefiFindRsdp(BOOT_UEFI_CONTEXT* Context) {
    if (Context == NULL || Context->SystemTable == NULL) {
        return U64_FromU32(0u);
    }

    EFI_SYSTEM_TABLE* SystemTable = Context->SystemTable;
    if (SystemTable->ConfigurationTable == NULL || SystemTable->NumberOfTableEntries == 0u) {
        return U64_FromU32(0u);
    }

    EFI_GUID Acpi20Guid = {
        0x8868E871u, 0xE4F1u, 0x11D3u,
        {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}
    };
    EFI_GUID Acpi10Guid = {
        0xEB9D2D30u, 0x2D88u, 0x11D3u,
        {0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}
    };

    EFI_CONFIGURATION_TABLE* Tables = (EFI_CONFIGURATION_TABLE*)SystemTable->ConfigurationTable;
    EFI_PHYSICAL_ADDRESS RsdpPhysical = U64_FromU32(0u);

    for (EFI_UINTN Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
        EFI_CONFIGURATION_TABLE* Entry = &Tables[Index];
        if (BootUefiGuidEquals(&Entry->VendorGuid, &Acpi20Guid) != FALSE) {
            RsdpPhysical = U64_FromUINT((UINT)(EFI_UINTN)Entry->VendorTable);
            BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiFindRsdp] ACPI 2.0 RSDP ", RsdpPhysical);
            return RsdpPhysical;
        }
        if (U64_EQUAL(RsdpPhysical, U64_FromU32(0u)) &&
            BootUefiGuidEquals(&Entry->VendorGuid, &Acpi10Guid) != FALSE) {
            RsdpPhysical = U64_FromUINT((UINT)(EFI_UINTN)Entry->VendorTable);
        }
    }

    if (U64_EQUAL(RsdpPhysical, U64_FromU32(0u)) == FALSE) {
        BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiFindRsdp] ACPI 1.0 RSDP ", RsdpPhysical);
    }

    return RsdpPhysical;
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
    BootUefiOutputHex64(
        Context->ConsoleOut,
        "[BootUefiGetMemoryMap] MemoryMap buffer ",
        U64_FromU32((U32)(EFI_UINTN)MemoryMap));
    BootUefiOutputHex64(
        Context->ConsoleOut,
        "[BootUefiGetMemoryMap] MemoryMapSize (alloc) ",
        U64_FromU32((U32)MemoryMapSize));
    if (Status != EFI_SUCCESS || MemoryMap == NULL) {
        return Status;
    }

    Status = Context->BootServices->GetMemoryMap(
        &MemoryMapSize,
        MemoryMap,
        MapKeyOut,
        DescriptorSizeOut,
        DescriptorVersionOut);
    BootUefiOutputStatus(Context->ConsoleOut, "[BootUefiGetMemoryMap] GetMemoryMap(data) ", Status);
    BootUefiOutputHex64(
        Context->ConsoleOut,
        "[BootUefiGetMemoryMap] MemoryMapSize (data) ",
        U64_FromU32((U32)MemoryMapSize));
    BootUefiOutputHex64(Context->ConsoleOut, "[BootUefiGetMemoryMap] MapKey ", U64_FromU32((U32)(*MapKeyOut)));
    BootUefiOutputHex64(
        Context->ConsoleOut,
        "[BootUefiGetMemoryMap] DescriptorSize ",
        U64_FromU32((U32)(*DescriptorSizeOut)));
    BootUefiOutputHex32(Context->ConsoleOut, "[BootUefiGetMemoryMap] DescriptorVersion ", (U32)(*DescriptorVersionOut));

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
 * @brief Retrieve the UEFI memory map without emitting console output.
 *
 * @param Context UEFI context.
 * @param MemoryMapOut Receives the allocated memory map buffer.
 * @param MemoryMapSizeOut Receives the map size.
 * @param MapKeyOut Receives the map key.
 * @param DescriptorSizeOut Receives descriptor size.
 * @param DescriptorVersionOut Receives descriptor version.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiGetMemoryMapSilent(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* MapKeyOut,
    EFI_UINTN* DescriptorSizeOut,
    EFI_UINTN* DescriptorVersionOut) {
    EFI_UINTN MemoryMapSize = 0;
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    UINT Attempts = 0;

    if (Context == NULL || Context->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    for (;;) {
        Status = Context->BootServices->GetMemoryMap(
            &MemoryMapSize,
            NULL,
            MapKeyOut,
            DescriptorSizeOut,
            DescriptorVersionOut);

        if (Status != EFI_BUFFER_TOO_SMALL) {
            return Status;
        }

        EFI_UINTN DescriptorSize = *DescriptorSizeOut;
        if (DescriptorSize == 0u) {
            DescriptorSize = 0x30u;
        }
        MemoryMapSize += EFI_PAGE_SIZE + (DescriptorSize * 4u);
        Status = Context->BootServices->AllocatePool(EfiLoaderData, MemoryMapSize, (void**)&MemoryMap);
        if (Status != EFI_SUCCESS || MemoryMap == NULL) {
            return Status;
        }

        Status = Context->BootServices->GetMemoryMap(
            &MemoryMapSize,
            MemoryMap,
            MapKeyOut,
            DescriptorSizeOut,
            DescriptorVersionOut);

        if (Status == EFI_SUCCESS) {
            break;
        }

        Context->BootServices->FreePool(MemoryMap);
        MemoryMap = NULL;
        MemoryMapSize = 0;

        if (Status != EFI_BUFFER_TOO_SMALL) {
            return Status;
        }

        Attempts++;
        if (Attempts >= 8u) {
            return Status;
        }
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

static U32 BootUefiMaskPosition(U32 Mask) {
    if (Mask == 0u) {
        return 0u;
    }

    U32 Position = 0u;
    while ((Mask & 1u) == 0u) {
        Mask >>= 1;
        Position++;
    }

    return Position;
}

/************************************************************************/

static U32 BootUefiMaskSize(U32 Mask) {
    U32 Count = 0u;
    while (Mask != 0u) {
        if ((Mask & 1u) != 0u) {
            Count++;
        }
        Mask >>= 1;
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Scale an 8-bit color channel to an arbitrary mask width.
 *
 * @param Value Channel value in [0, 255].
 * @param MaskSize Number of bits in the destination channel.
 * @return Scaled value limited to mask width.
 */
static U32 BootUefiScaleColorToMask(U8 Value, U32 MaskSize) {
    if (MaskSize == 0u) {
        return 0u;
    }

    if (MaskSize >= 32u) {
        return (U32)Value;
    }

    U32 MaxValue = (1u << MaskSize) - 1u;
    return ((U32)Value * MaxValue) / 255u;
}

/************************************************************************/

/**
 * @brief Resolve and cache GOP pointer while boot services are available.
 *
 * @param Context Boot context.
 * @return Cached GOP pointer, or NULL if unavailable.
 */
static EFI_GRAPHICS_OUTPUT_PROTOCOL* BootUefiGetGraphicsOutput(BOOT_UEFI_CONTEXT* Context) {
    if (Context == NULL) {
        return NULL;
    }

    if (Context->GraphicsOutput != NULL) {
        return Context->GraphicsOutput;
    }

    if (Context->BootServicesExited == TRUE || Context->BootServices == NULL) {
        return NULL;
    }

    EFI_GUID GraphicsOutputGuid = {
        0x9042A9DEu, 0x23DCu, 0x4A38u,
        {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}
    };

    EFI_GRAPHICS_OUTPUT_PROTOCOL* Graphics = NULL;
    EFI_STATUS Status = Context->BootServices->LocateProtocol(
        &GraphicsOutputGuid,
        NULL,
        (void**)&Graphics);
    if (Status != EFI_SUCCESS || Graphics == NULL || Graphics->Mode == NULL || Graphics->Mode->Info == NULL) {
        return NULL;
    }

    Context->GraphicsOutput = Graphics;
    return Context->GraphicsOutput;
}

/************************************************************************/

/**
 * @brief Compose a native framebuffer pixel value from RGB channels.
 *
 * @param Info GOP mode information.
 * @param Red Red channel (8-bit).
 * @param Green Green channel (8-bit).
 * @param Blue Blue channel (8-bit).
 * @return Pixel value in framebuffer format.
 */
static U32 BootUefiComposePixelColor(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info, U8 Red, U8 Green, U8 Blue) {
    if (Info == NULL) {
        return 0u;
    }

    if (Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        return (U32)Red | ((U32)Green << 8) | ((U32)Blue << 16);
    }

    if (Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        return (U32)Blue | ((U32)Green << 8) | ((U32)Red << 16);
    }

    if (Info->PixelFormat == PixelBitMask) {
        U32 RedShift = BootUefiMaskPosition(Info->PixelInformation.RedMask);
        U32 GreenShift = BootUefiMaskPosition(Info->PixelInformation.GreenMask);
        U32 BlueShift = BootUefiMaskPosition(Info->PixelInformation.BlueMask);
        U32 RedValue = BootUefiScaleColorToMask(Red, BootUefiMaskSize(Info->PixelInformation.RedMask));
        U32 GreenValue = BootUefiScaleColorToMask(Green, BootUefiMaskSize(Info->PixelInformation.GreenMask));
        U32 BlueValue = BootUefiScaleColorToMask(Blue, BootUefiMaskSize(Info->PixelInformation.BlueMask));
        return (RedValue << RedShift) | (GreenValue << GreenShift) | (BlueValue << BlueShift);
    }

    return 0u;
}

/************************************************************************/

/**
 * @brief Draw one colored boot stage marker in the top-left corner.
 *
 * @param Context Boot context.
 * @param StageIndex Marker index on the first line.
 * @param Red Marker red channel.
 * @param Green Marker green channel.
 * @param Blue Marker blue channel.
 */
#if BOOT_STAGE_MARKERS == 1
static void BootUefiMarkStage(BOOT_UEFI_CONTEXT* Context, U32 StageIndex, U8 Red, U8 Green, U8 Blue) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Graphics = BootUefiGetGraphicsOutput(Context);
    if (Graphics == NULL || Graphics->Mode == NULL || Graphics->Mode->Info == NULL) {
        return;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = Graphics->Mode->Info;
    if (Info->PixelFormat == PixelBltOnly) {
        return;
    }

    U32 BytesPerPixel = 4u;
    if (Info->PixelFormat == PixelBitMask) {
        U32 AllMask = Info->PixelInformation.RedMask |
                      Info->PixelInformation.GreenMask |
                      Info->PixelInformation.BlueMask |
                      Info->PixelInformation.ReservedMask;
        U32 Highest = 0u;
        while (AllMask != 0u) {
            Highest++;
            AllMask >>= 1;
        }
        if (Highest == 0u) {
            return;
        }
        BytesPerPixel = (Highest + 7u) / 8u;
    }

    if (BytesPerPixel == 0u || BytesPerPixel > 4u) {
        return;
    }

    const U32 MarkerSize = 8u;
    const U32 MarkerSpacing = 2u;
    const U32 MarkerGroupSize = 10u;
    const U32 MarkerLineStride = MarkerSize + MarkerSpacing;
    U32 GroupIndex = StageIndex / MarkerGroupSize;
    U32 GroupOffset = StageIndex % MarkerGroupSize;
    U32 StartX = 2u + GroupOffset * (MarkerSize + MarkerSpacing);
    U32 StartY = 2u + GroupIndex * MarkerLineStride;
    if (StartX >= Info->HorizontalResolution || StartY >= Info->VerticalResolution) {
        return;
    }

    U32 DrawWidth = MarkerSize;
    U32 DrawHeight = MarkerSize;
    if (StartX + DrawWidth > Info->HorizontalResolution) {
        DrawWidth = Info->HorizontalResolution - StartX;
    }
    if (StartY + DrawHeight > Info->VerticalResolution) {
        DrawHeight = Info->VerticalResolution - StartY;
    }

    U32 Pixel = BootUefiComposePixelColor(Info, Red, Green, Blue);
    U8* FrameBuffer = (U8*)BootUefiPhysicalToPointer(Graphics->Mode->FrameBufferBase);
    if (FrameBuffer == NULL) {
        return;
    }

    U32 Pitch = Info->PixelsPerScanLine * BytesPerPixel;
    for (U32 Y = 0u; Y < DrawHeight; Y++) {
        U8* Row = FrameBuffer + ((StartY + Y) * Pitch) + (StartX * BytesPerPixel);
        for (U32 X = 0u; X < DrawWidth; X++) {
            Row[0] = (U8)(Pixel & 0xFFu);
            if (BytesPerPixel > 1u) {
                Row[1] = (U8)((Pixel >> 8) & 0xFFu);
            }
            if (BytesPerPixel > 2u) {
                Row[2] = (U8)((Pixel >> 16) & 0xFFu);
            }
            if (BytesPerPixel > 3u) {
                Row[3] = (U8)((Pixel >> 24) & 0xFFu);
            }
            Row += BytesPerPixel;
        }
    }
}
#else
static void BootUefiMarkStage(BOOT_UEFI_CONTEXT* Context, U32 StageIndex, U8 Red, U8 Green, U8 Blue) {
    UNUSED(Context);
    UNUSED(StageIndex);
    UNUSED(Red);
    UNUSED(Green);
    UNUSED(Blue);
}
#endif

/************************************************************************/

static void NORETURN BootUefiHaltNoServices(void) {
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/************************************************************************/

static BOOL BootUefiGetFramebufferInfo(BOOT_UEFI_CONTEXT* Context, BOOT_FRAMEBUFFER_INFO* FramebufferInfo) {
    if (Context == NULL || FramebufferInfo == NULL) {
        return FALSE;
    }

    EFI_GUID GraphicsOutputGuid = {
        0x9042A9DEu, 0x23DCu, 0x4A38u,
        {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}
    };

    EFI_GRAPHICS_OUTPUT_PROTOCOL* Graphics = NULL;
    EFI_STATUS Status = Context->BootServices->LocateProtocol(
        &GraphicsOutputGuid,
        NULL,
        (void**)&Graphics);
    if (Status != EFI_SUCCESS || Graphics == NULL || Graphics->Mode == NULL || Graphics->Mode->Info == NULL) {
        return FALSE;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = Graphics->Mode->Info;
    if (Info->PixelFormat == PixelBltOnly) {
        return FALSE;
    }

    MemorySet(FramebufferInfo, 0, sizeof(*FramebufferInfo));
    FramebufferInfo->Type = MULTIBOOT_FRAMEBUFFER_RGB;
    FramebufferInfo->Address = Graphics->Mode->FrameBufferBase;
    FramebufferInfo->Width = Info->HorizontalResolution;
    FramebufferInfo->Height = Info->VerticalResolution;

    U32 BitsPerPixel = 32u;
    if (Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        FramebufferInfo->RedPosition = 0u;
        FramebufferInfo->RedMaskSize = 8u;
        FramebufferInfo->GreenPosition = 8u;
        FramebufferInfo->GreenMaskSize = 8u;
        FramebufferInfo->BluePosition = 16u;
        FramebufferInfo->BlueMaskSize = 8u;
    } else if (Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        FramebufferInfo->BluePosition = 0u;
        FramebufferInfo->BlueMaskSize = 8u;
        FramebufferInfo->GreenPosition = 8u;
        FramebufferInfo->GreenMaskSize = 8u;
        FramebufferInfo->RedPosition = 16u;
        FramebufferInfo->RedMaskSize = 8u;
    } else if (Info->PixelFormat == PixelBitMask) {
        FramebufferInfo->RedPosition = BootUefiMaskPosition(Info->PixelInformation.RedMask);
        FramebufferInfo->RedMaskSize = BootUefiMaskSize(Info->PixelInformation.RedMask);
        FramebufferInfo->GreenPosition = BootUefiMaskPosition(Info->PixelInformation.GreenMask);
        FramebufferInfo->GreenMaskSize = BootUefiMaskSize(Info->PixelInformation.GreenMask);
        FramebufferInfo->BluePosition = BootUefiMaskPosition(Info->PixelInformation.BlueMask);
        FramebufferInfo->BlueMaskSize = BootUefiMaskSize(Info->PixelInformation.BlueMask);
        U32 AllMask = Info->PixelInformation.RedMask |
                      Info->PixelInformation.GreenMask |
                      Info->PixelInformation.BlueMask |
                      Info->PixelInformation.ReservedMask;
        if (AllMask != 0u) {
            U32 Highest = 0u;
            U32 Temp = AllMask;
            while (Temp != 0u) {
                Highest++;
                Temp >>= 1;
            }
            BitsPerPixel = Highest;
        }
    }

    FramebufferInfo->BitsPerPixel = BitsPerPixel;
    FramebufferInfo->Pitch = Info->PixelsPerScanLine * (BitsPerPixel / 8u);
    return TRUE;
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
 * @brief Open the root folder and prepare the boot context.
 *
 * @param Context Boot context.
 * @param RootFileOut Receives opened root folder protocol.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiOpenRootFolder(BOOT_UEFI_CONTEXT* Context, EFI_FILE_PROTOCOL** RootFileOut) {
    if (Context == NULL || RootFileOut == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status = BootUefiOpenRootFileSystem(Context, RootFileOut);
    if (Status != EFI_SUCCESS || *RootFileOut == NULL) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot open root folder\r\n");
        return Status;
    }

    BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] Root folder opened\r\n");
    BootUefiDisableWatchdog(Context);
    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Load the kernel image into a firmware-selected physical destination.
 *
 * @param Context Boot context.
 * @param RootFile Opened root folder protocol.
 * @param KernelPath UTF-16 kernel file path.
 * @param FileSizeOut Receives kernel file size.
 * @param KernelPhysicalBaseOut Receives kernel physical base (32-bit).
 * @return EFI status code.
 */
static EFI_STATUS BootUefiLoadKernelImage(
    BOOT_UEFI_CONTEXT* Context,
    EFI_FILE_PROTOCOL* RootFile,
    const CHAR16* KernelPath,
    UINT* FileSizeOut,
    U32* KernelPhysicalBaseOut,
    U32* KernelReservedBytesOut) {
    if (Context == NULL || RootFile == NULL || KernelPath == NULL || FileSizeOut == NULL ||
        KernelPhysicalBaseOut == NULL || KernelReservedBytesOut == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    UINT FileSize = 0;
    EFI_STATUS Status = BootUefiGetFileSize(Context, RootFile, KernelPath, &FileSize);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot read kernel size\r\n");
        BootUefiOutputStatus(Context->ConsoleOut, "[EfiMain] Status ", Status);
        return Status;
    }
    if (FileSize == 0u) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Kernel size is zero\r\n");
        return EFI_BUFFER_TOO_SMALL;
    }
    // Keep early logs explicit to simplify boot debugging in firmware consoles.
    BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] Kernel size ", FileSize);

    UINT KernelMapBytes = BootUefiAlignUp((UINT)(FileSize + BOOT_KERNEL_MAP_PADDING_BYTES), EFI_PAGE_SIZE);
    UINT KernelTableWorkspaceBytes = BOOT_KERNEL_TABLE_WORKSPACE_BYTES;
    UINT KernelReservedBytes = KernelMapBytes + KernelTableWorkspaceBytes;
    UINT KernelPages = BootUefiAlignUp(KernelReservedBytes, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS KernelAddress = BootUefiPhysicalFromU32(0xFFFFFFFFu);
    Status = Context->BootServices->AllocatePages(
        EFI_ALLOCATE_MAX_ADDRESS,
        EfiLoaderData,
        KernelPages,
        &KernelAddress);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot reserve kernel pages\r\n");
        return Status;
    }
    U64 KernelAddress64 = BootUefiPhysicalToU64(KernelAddress);
    if (U64_High32(KernelAddress64) != 0u) {
        Context->BootServices->FreePages(KernelAddress, KernelPages);
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Kernel physical base above 4GB\r\n");
        return EFI_INVALID_PARAMETER;
    }
    U32 KernelPhysicalBase = U64_Low32(KernelAddress64);
    BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] Kernel physical base ", KernelPhysicalBase);
    BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] Kernel pages ", KernelPages);
    BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] Kernel reserved bytes ", (U32)KernelReservedBytes);

    Status = BootUefiReadFile(Context, RootFile, KernelPath, KernelAddress, FileSize);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot read kernel file\r\n");
        return Status;
    }

    BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] Kernel loaded\r\n");
    *FileSizeOut = FileSize;
    *KernelPhysicalBaseOut = KernelPhysicalBase;
    *KernelReservedBytesOut = (U32)KernelReservedBytes;
    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Allocate and layout all multiboot structures in one buffer.
 *
 * @param Context Boot context.
 * @param KernelFileName Kernel command line text.
 * @param LayoutOut Receives pointers to all multiboot regions.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiAllocateMultibootData(
    BOOT_UEFI_CONTEXT* Context,
    LPCSTR KernelFileName,
    BOOT_UEFI_MULTIBOOT_LAYOUT* LayoutOut) {
    if (Context == NULL || KernelFileName == NULL || LayoutOut == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    // Reserve one contiguous low-memory block for all multiboot payload sections.
    UINT MultibootBytes =
        (UINT)sizeof(multiboot_info_t) +
        (UINT)(sizeof(multiboot_memory_map_t) * E820_MAX_ENTRIES) +
        (UINT)sizeof(multiboot_module_t) +
        (UINT)(StringLength(KernelFileName) + 1u) +
        (UINT)(StringLength(BootloaderNameText) + 1u);
    UINT MultibootPages = BootUefiAlignUp(MultibootBytes, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;

    EFI_PHYSICAL_ADDRESS MultibootBase = BootUefiPhysicalFromU32(0x001FFFFFu);
    EFI_STATUS Status = Context->BootServices->AllocatePages(
        EFI_ALLOCATE_MAX_ADDRESS,
        EfiLoaderData,
        MultibootPages,
        &MultibootBase);
    if (Status != EFI_SUCCESS) {
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot allocate Multiboot data\r\n");
        return Status;
    }

    // Slice the block with explicit alignment to match multiboot structure expectations.
    MemorySet(LayoutOut, 0, sizeof(*LayoutOut));
    U8* MultibootCursor = (U8*)BootUefiPhysicalToPointer(MultibootBase);

    LayoutOut->MultibootInfo = (multiboot_info_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_info_t);
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    LayoutOut->MultibootMemoryMap = (multiboot_memory_map_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_memory_map_t) * E820_MAX_ENTRIES;
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    LayoutOut->KernelModule = (multiboot_module_t*)MultibootCursor;
    MultibootCursor += sizeof(multiboot_module_t);
    MultibootCursor = BootUefiAlignPointer(MultibootCursor, 8u);

    LayoutOut->BootloaderName = (LPSTR)MultibootCursor;
    StringCopy(LayoutOut->BootloaderName, BootloaderNameText);
    MultibootCursor += StringLength(LayoutOut->BootloaderName) + 1u;

    LayoutOut->KernelCommandLine = (LPSTR)MultibootCursor;
    StringCopy(LayoutOut->KernelCommandLine, KernelFileName);
    return EFI_SUCCESS;
}

/************************************************************************/

/**
 * @brief Get the RSDP address truncated to 32 bits for multiboot.
 *
 * @param Context Boot context.
 * @return Lower 32 bits of RSDP physical address, or zero when unavailable.
 */
static U32 BootUefiGetRsdpPhysicalLow(BOOT_UEFI_CONTEXT* Context) {
    EFI_PHYSICAL_ADDRESS RsdpPhysical = BootUefiFindRsdp(Context);
    U32 RsdpPhysicalLow = 0u;
    if (U64_EQUAL(RsdpPhysical, U64_FromU32(0u)) == FALSE) {
        if (U64_High32(RsdpPhysical) != 0u) {
            BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] WARNING: RSDP above 4GB not supported\r\n");
        } else {
            RsdpPhysicalLow = U64_Low32(RsdpPhysical);
        }
    }

    return RsdpPhysicalLow;
}

/************************************************************************/

/**
 * @brief Exit UEFI boot services with retries on stale map keys.
 *
 * @param Context Boot context.
 * @param MemoryMapOut Receives final memory map buffer.
 * @param MemoryMapSizeOut Receives memory map size.
 * @param DescriptorSizeOut Receives memory map descriptor size.
 * @return EFI status code.
 */
static EFI_STATUS BootUefiExitBootServicesWithRetry(
    BOOT_UEFI_CONTEXT* Context,
    EFI_MEMORY_DESCRIPTOR** MemoryMapOut,
    EFI_UINTN* MemoryMapSizeOut,
    EFI_UINTN* DescriptorSizeOut) {
    if (Context == NULL || MemoryMapOut == NULL || MemoryMapSizeOut == NULL || DescriptorSizeOut == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    EFI_UINTN MemoryMapSize = 0;
    EFI_UINTN MemoryMapCapacity = 0;
    EFI_UINTN MapKey = 0;
    EFI_UINTN DescriptorSize = 0;
    EFI_UINTN DescriptorVersion = 0;
    UINT ExitBootAttempts = 0;

    // Allocate a large reusable map buffer once, before retry attempts.
    EFI_STATUS Status = Context->BootServices->GetMemoryMap(
        &MemoryMapSize,
        NULL,
        &MapKey,
        &DescriptorSize,
        &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    if (DescriptorSize == 0u) {
        DescriptorSize = 0x30u;
    }
    MemoryMapCapacity = BootUefiAlignUp(
        (UINT)(MemoryMapSize + (DescriptorSize * 0x40u) + (EFI_PAGE_SIZE * 2u)),
        EFI_PAGE_SIZE);
    Status = Context->BootServices->AllocatePool(EfiLoaderData, MemoryMapCapacity, (void**)&MemoryMap);
    if (Status != EFI_SUCCESS || MemoryMap == NULL) {
        return Status;
    }

    for (;;) {
        // Always refresh the map just before ExitBootServices to get a valid MapKey.
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] Preparing memory map\r\n");
        MemoryMapSize = MemoryMapCapacity;
        Status = Context->BootServices->GetMemoryMap(
            &MemoryMapSize,
            MemoryMap,
            &MapKey,
            &DescriptorSize,
            &DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            // Grow once in place when firmware map unexpectedly expanded.
            Context->BootServices->FreePool(MemoryMap);
            MemoryMap = NULL;
            MemoryMapCapacity = BootUefiAlignUp(
                (UINT)(MemoryMapSize + (DescriptorSize * 0x40u) + (EFI_PAGE_SIZE * 2u)),
                EFI_PAGE_SIZE);
            Status = Context->BootServices->AllocatePool(EfiLoaderData, MemoryMapCapacity, (void**)&MemoryMap);
            if (Status != EFI_SUCCESS || MemoryMap == NULL) {
                return Status;
            }
            continue;
        }
        if (Status != EFI_SUCCESS) {
            BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: Cannot get memory map\r\n");
            Context->BootServices->FreePool(MemoryMap);
            return Status;
        }
        UNUSED(DescriptorVersion);

        // Do not call any boot services between GetMemoryMap and ExitBootServices.
        Status = Context->BootServices->ExitBootServices(Context->ImageHandle, MapKey);
        if (Status == EFI_SUCCESS) {
            *MemoryMapOut = MemoryMap;
            *MemoryMapSizeOut = MemoryMapSize;
            *DescriptorSizeOut = DescriptorSize;
            return EFI_SUCCESS;
        }

        ExitBootAttempts++;
        BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ExitBootServices retry\r\n");
        BootUefiOutputStatus(Context->ConsoleOut, "[EfiMain] ExitBootServices status ", Status);
        BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] ExitBootServices map key ", (U32)MapKey);
        BootUefiOutputHex32(Context->ConsoleOut, "[EfiMain] ExitBootServices attempt ", (U32)ExitBootAttempts);
        // Reuse the same buffer so retry iterations do not perturb the map.
        MemoryMapSize = MemoryMapCapacity;
        MapKey = 0;
        DescriptorSize = 0x30u;
        DescriptorVersion = 0;

        if (Status != EFI_INVALID_PARAMETER) {
            BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: ExitBootServices failed\r\n");
            Context->BootServices->FreePool(MemoryMap);
            return Status;
        }

        if (ExitBootAttempts >= 4u) {
            BootUefiOutputAscii(Context->ConsoleOut, "[EfiMain] ERROR: ExitBootServices retry limit reached\r\n");
            Context->BootServices->FreePool(MemoryMap);
            return Status;
        }
    }
}

/************************************************************************/

/**
 * @brief Publish handoff parameters and jump to the protected-mode entry.
 *
 * @param FileSize Kernel file size.
 * @param MultibootInfoPtr Multiboot info physical pointer.
 * @param KernelPhysicalBase Kernel physical base address.
 * @param UefiImageBase UEFI image base address.
 * @param UefiImageSize UEFI image size.
 */
static void NORETURN BootUefiEnterKernel(
    U32 FileSize,
    U32 MultibootInfoPtr,
    U32 KernelPhysicalBase,
    U64 UefiImageBase,
    U64 UefiImageSize) {
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] ExitBootServices ok\r\n");
    UefiStubMultibootInfoPtr = MultibootInfoPtr;
    UefiStubMultibootMagic = MULTIBOOT_BOOTLOADER_MAGIC;
    UefiStubKernelPhysicalBase = KernelPhysicalBase;
#if UEFI_STUB_TEST == 1
    UefiStubTestOnly = 1u;
#else
    UefiStubTestOnly = 0u;
#endif
    __asm__ __volatile__("" ::: "memory");
#if UEFI_EARLY_HALT == 1
    BootUefiHaltNoServices();
#endif
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Calling EnterProtectedPagingAndJump\r\n");
    EnterProtectedPagingAndJump(FileSize, MultibootInfoPtr, UefiImageBase, UefiImageSize);
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Returned from EnterProtectedPagingAndJump\r\n");
    __asm__ __volatile__("ud2");
    Hang();
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
    // Build a compact context passed through all boot stages.
    BOOT_UEFI_CONTEXT Context = {
        .ImageHandle = ImageHandle,
        .SystemTable = SystemTable,
        .BootServices = SystemTable->BootServices,
        .ConsoleOut = SystemTable->ConOut,
        .GraphicsOutput = NULL,
        .BootServicesExited = FALSE
    };

    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_BOOT_START, 255u, 0u, 0u);
    BootUefiOutputAscii(Context.ConsoleOut, "[EfiMain] Starting EXOS UEFI boot\r\n");
    BootUefiDebugTransportInit(&Context);
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Debug transport initialized\r\n");
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_DEBUG_TRANSPORT_READY, 255u, 128u, 0u);

    EFI_FILE_PROTOCOL* RootFile = NULL;
    EFI_STATUS Status = BootUefiOpenRootFolder(&Context, &RootFile);
    if (Status != EFI_SUCCESS) {
        return Status;
    }
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Root folder opened\r\n");
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_ROOT_FOLDER_OPENED, 255u, 255u, 0u);

    const CHAR16 KernelPath[] = { '\\', 'e', 'x', 'o', 's', '.', 'b', 'i', 'n', 0 };
    LPCSTR KernelFileName = KernelFileNameText;

    // Load the kernel image before leaving boot services.
    UINT FileSize = 0;
    U32 KernelPhysicalBase = 0u;
    U32 KernelReservedBytes = 0u;
    Status = BootUefiLoadKernelImage(
        &Context,
        RootFile,
        KernelPath,
        &FileSize,
        &KernelPhysicalBase,
        &KernelReservedBytes);
    RootFile->Close(RootFile);
    if (Status != EFI_SUCCESS) {
        return Status;
    }
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Kernel loaded\r\n");
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_KERNEL_LOADED, 0u, 255u, 0u);

    BOOT_UEFI_MULTIBOOT_LAYOUT MultibootLayout;
    // Allocate and prepare multiboot buffers while firmware allocators are available.
    Status = BootUefiAllocateMultibootData(&Context, KernelFileName, &MultibootLayout);
    if (Status != EFI_SUCCESS) {
        return Status;
    }
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Multiboot data allocated\r\n");
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_MULTIBOOT_ALLOCATED, 0u, 255u, 255u);

    E820ENTRY E820Map[E820_MAX_ENTRIES];
    MemorySet(E820Map, 0, sizeof(E820Map));

    BOOT_FRAMEBUFFER_INFO FramebufferInfo;
    // Framebuffer data is optional and only attached when graphics mode is valid.
    BOOL HasFramebuffer = BootUefiGetFramebufferInfo(&Context, &FramebufferInfo);
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_FRAMEBUFFER_QUERIED, 0u, 128u, 255u);

#if UEFI_STUB_EARLY_CALL == 1
    UefiStubMultibootInfoPtr = 0u;
    UefiStubMultibootMagic = 0u;
    UefiStubTestOnly = 1u;
    UefiStubKernelPhysicalBase = 0u;
    __asm__ __volatile__("" ::: "memory");
    EnterProtectedPagingAndJump(0u, 0u, Context.ImageBase, Context.ImageSize);
    BootUefiHalt(&Context, "UEFI_STUB_EARLY_CALL returned");
#endif

    U32 RsdpPhysicalLow = BootUefiGetRsdpPhysicalLow(&Context);
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] RSDP captured\r\n");
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_RSDP_CAPTURED, 0u, 0u, 255u);

    EFI_MEMORY_DESCRIPTOR* MemoryMap = NULL;
    EFI_UINTN MemoryMapSize = 0;
    EFI_UINTN DescriptorSize = 0;
    // This is the last point where boot services are callable.
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_EXIT_BOOT_BEGIN, 128u, 0u, 255u);
    BootUefiDebugTransportWrite((LPCSTR)"[EfiMain] Entering ExitBootServices\r\n");
    Status = BootUefiExitBootServicesWithRetry(&Context, &MemoryMap, &MemoryMapSize, &DescriptorSize);
    if (Status != EFI_SUCCESS) {
        return Status;
    }
    Context.BootServicesExited = TRUE;
    BootUefiDebugTransportNotifyExitBootServices();
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_EXIT_BOOT_DONE, 255u, 0u, 255u);

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
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_E820_READY, 255u, 255u, 255u);

    U32 MultibootInfoPtr = BootBuildMultibootInfo(
        MultibootLayout.MultibootInfo,
        MultibootLayout.MultibootMemoryMap,
        MultibootLayout.KernelModule,
        E820Map,
        E820Count,
        KernelPhysicalBase,
        (U32)FileSize,
        KernelReservedBytes,
        RsdpPhysicalLow,
        MultibootLayout.BootloaderName,
        MultibootLayout.KernelCommandLine,
        HasFramebuffer ? &FramebufferInfo : NULL);
    BootUefiMarkStage(&Context, BOOT_UEFI_STAGE_MULTIBOOT_READY, 128u, 128u, 128u);

    // No return path after this call.
    BootUefiEnterKernel((U32)FileSize, MultibootInfoPtr, KernelPhysicalBase, Context.ImageBase, Context.ImageSize);

    return EFI_SUCCESS;
}
