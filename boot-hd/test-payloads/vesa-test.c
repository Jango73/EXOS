
/************************************************************************\

    EXOS VESA Test Payload
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


    VESA Graphics Test - Set 640x480x24 mode and draw random rectangles

\************************************************************************/

// X86_32 16-bit real mode VESA test payload
// Sets 640x480x24 mode and draws random rectangles

#include "../../kernel/include/arch/x86-32/x86-32.h"
#include "../../kernel/include/SerialPort.h"
#include "../../kernel/include/CoreString.h"
#include "../include/vbr-realmode-utils.h"

/************************************************************************/

__asm__(".code16gcc");

/************************************************************************/
// VESA structures and constants

typedef struct PACKED {
    U8 VESASignature[4];
    U16 VESAVersion;
    U32 OEMStringPtr;
    U8 Capabilities[4];
    U32 VideoModePtr;
    U16 TotalMemory;
    U16 OEMSoftwareRev;
    U32 OEMVendorNamePtr;
    U32 OEMProductNamePtr;
    U32 OEMProductRevPtr;
    U8 Reserved[222];
    U8 OEMData[256];
} VESA_INFO;

typedef struct PACKED {
    U16 ModeAttributes;
    U8 WinAAttributes;
    U8 WinBAttributes;
    U16 WinGranularity;
    U16 WinSize;
    U16 WinASegment;
    U16 WinBSegment;
    U32 WinFuncPtr;
    U16 BytesPerScanLine;
    U16 XResolution;
    U16 YResolution;
    U8 XCharSize;
    U8 YCharSize;
    U8 NumberOfPlanes;
    U8 BitsPerPixel;
    U8 NumberOfBanks;
    U8 MemoryModel;
    U8 BankSize;
    U8 NumberOfImagePages;
    U8 Reserved;
    U8 RedMaskSize;
    U8 RedFieldPosition;
    U8 GreenMaskSize;
    U8 GreenFieldPosition;
    U8 BlueMaskSize;
    U8 BlueFieldPosition;
    U8 RsvdMaskSize;
    U8 RsvdFieldPosition;
    U8 DirectColorModeInfo;
    U32 PhysBasePtr;
    U32 OffScreenMemOffset;
    U16 OffScreenMemSize;
    U8 Reserved2[206];
} VESA_MODE_INFO;

#define VESA_MODE_640x480x24 0x112
#define VESA_LINEAR_FRAME_BUFFER 0x4000

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

static const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};
STR TempString[128];
static VESA_MODE_INFO ModeInfo;
static U32 FrameBuffer = 0;
static U32 LinearRandom = 1;

/************************************************************************/

static inline U8 InPortByte(U16 Port) {
    U8 Val;
    __asm__ __volatile__("inb %1, %0" : "=a"(Val) : "Nd"(Port));
    return Val;
}

static inline void OutPortByte(U16 Port, U8 Val) {
    __asm__ __volatile__("outb %0, %1" ::"a"(Val), "Nd"(Port));
}

/************************************************************************/

void SerialReset(U8 Which) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    OutPortByte(base + UART_IER, 0x00);
    OutPortByte(base + UART_LCR, LCR_DLAB);
    OutPortByte(base + UART_DLL, (U8)(BAUD_DIV_38400 & 0xFF));
    OutPortByte(base + UART_DLM, (U8)((BAUD_DIV_38400 >> 8) & 0xFF));
    OutPortByte(base + UART_LCR, LCR_8N1);
    OutPortByte(base + UART_FCR, (U8)(FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX | FCR_TRIG_14));
    OutPortByte(base + UART_MCR, (U8)(MCR_DTR | MCR_RTS | MCR_OUT2));
}

/************************************************************************/

void SerialOut(U8 Which, U8 Char) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    const U32 MaxSpin = 100000;
    U32 spins = 0;

    while (!(InPortByte(base + UART_LSR) & LSR_THRE)) {
        if (++spins >= MaxSpin) return;
    }

    OutPortByte(base + UART_THR, Char);
}

/************************************************************************/

static U32 SimpleRandom(void) {
    LinearRandom = (LinearRandom * 1103515245U + 12345U) & 0x7FFFFFFF;
    return LinearRandom;
}

/************************************************************************/

static void DrawRectangle(U32 x, U32 y, U32 width, U32 height, U32 color) {
    for (U32 j = 0; j < height; j++) {
        for (U32 i = 0; i < width; i++) {
            SetPixel24(x + i, y + j, color, FrameBuffer);
        }
    }
}

/************************************************************************/

void BootMain(U32 BootDrive, U32 Fat32Lba) {
    UNUSED(BootDrive);
    UNUSED(Fat32Lba);
    InitDebug();

    WriteString(TEXT("\r\n"));
    WriteString(TEXT("***************************************\r\n"));
    WriteString(TEXT("*    EXOS VESA Graphics Test          *\r\n"));
    WriteString(TEXT("***************************************\r\n"));
    WriteString(TEXT("\r\n"));

    WriteString(TEXT("[VESA] Enabling A20 line...\r\n"));
    EnableA20();

    WriteString(TEXT("[VESA] Getting mode info for 640x480x24...\r\n"));

    if (VESAGetModeInfo(VESA_MODE_640x480x24, MakeSegOfs(&ModeInfo)) != 0) {
        WriteString(TEXT("[VESA] ERROR: Failed to get mode info\r\n"));
        Hang();
    }

    StringPrintFormat(TempString, TEXT("[VESA] Resolution: %dx%d\r\n"),
                      ModeInfo.XResolution, ModeInfo.YResolution);
    WriteString(TempString);

    StringPrintFormat(TempString, TEXT("[VESA] Bits per pixel: %d\r\n"),
                      ModeInfo.BitsPerPixel);
    WriteString(TempString);

    StringPrintFormat(TempString, TEXT("[VESA] Frame buffer: %x\r\n"),
                      ModeInfo.PhysBasePtr);
    WriteString(TempString);

    FrameBuffer = ModeInfo.PhysBasePtr;

    WriteString(TEXT("[VESA] Setting VESA mode 640x480x24...\r\n"));

    if (VESASetMode(VESA_MODE_640x480x24 | VESA_LINEAR_FRAME_BUFFER) != 0) {
        WriteString(TEXT("[VESA] ERROR: Failed to set VESA mode\r\n"));
        Hang();
    }

    WriteString(TEXT("[VESA] Mode set successfully\r\n"));
    WriteString(TEXT("[VESA] Drawing random rectangles...\r\n"));

    // Draw 400 random rectangles
    for (int i = 0; i < 400; i++) {
        U32 x = SimpleRandom() % 600;
        U32 y = SimpleRandom() % 440;
        U32 width = (SimpleRandom() % 40) + 10;
        U32 height = (SimpleRandom() % 40) + 10;

        U32 red = SimpleRandom() & 0xFF;
        U32 green = SimpleRandom() & 0xFF;
        U32 blue = SimpleRandom() & 0xFF;
        U32 color = (red << 16) | (green << 8) | blue;

        DrawRectangle(x, y, width, height, color);
    }

    WriteString(TEXT("[VESA] Drawing completed\r\n"));
    WriteString(TEXT("[VESA] Test will now halt\r\n"));

    // Hang forever
    Hang();
}
