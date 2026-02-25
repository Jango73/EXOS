
/************************************************************************\

    EXOS Kernel
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


    VESA

\************************************************************************/

#include "GFX.h"
#include "Arch.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"

/************************************************************************/

#define VESA_ENABLE_SELFTEST 1

/************************************************************************/

#define MKLINPTR(a) (((a & 0xFFFF0000) >> 12) + (a & 0x0000FFFF))

#define CLIPVALUE(val, min, max) \
    {                            \
        if (val < min)           \
            val = min;           \
        else if (val > max)      \
            val = max;           \
    }

/************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT VESACommands(UINT, UINT);

DRIVER DATA_SECTION VESADriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Video Electronics Standard Association",
    .Product = "VESA Compatible Graphics Card",
    .Flags = 0,
    .Command = VESACommands};

/************************************************************************/

/**
 * @brief Retrieves the VESA driver descriptor.
 * @return Pointer to the VESA driver.
 */
LPDRIVER VESAGetDriver(void) {
    return &VESADriver;
}

/************************************************************************/

typedef struct tag_VESA_CONTEXT VESA_CONTEXT, *LPVESA_CONTEXT;

/************************************************************************/

static COLOR SetPixel8(LPVESA_CONTEXT, I32, I32, COLOR);
static COLOR SetPixel16(LPVESA_CONTEXT, I32, I32, COLOR);
static COLOR SetPixel24(LPVESA_CONTEXT, I32, I32, COLOR);
static COLOR GetPixel8(LPVESA_CONTEXT, I32, I32);
static COLOR GetPixel16(LPVESA_CONTEXT, I32, I32);
static COLOR GetPixel24(LPVESA_CONTEXT, I32, I32);
static U32 Line8(LPVESA_CONTEXT, I32, I32, I32, I32);
static U32 Line16(LPVESA_CONTEXT, I32, I32, I32, I32);
static U32 Line24(LPVESA_CONTEXT, I32, I32, I32, I32);
static U32 Rect8(LPVESA_CONTEXT, I32, I32, I32, I32);
static U32 Rect16(LPVESA_CONTEXT, I32, I32, I32, I32);
static U32 Rect24(LPVESA_CONTEXT, I32, I32, I32, I32);

#if VESA_ENABLE_SELFTEST
static void VESADrawSelfTest(LPVESA_CONTEXT);
static U32 DATA_SECTION VESARectangleLogCount = 0;
#endif

/************************************************************************/

typedef struct tag_VESAINFOBLOCK {
    U8 Signature[4];  // 4 signature bytes
    U16 Version;      // VESA version number
    U32 OEMString;    // Pointer to OEM string
    U8 Caps[4];       // Capabilities of the video environment
    U32 ModePointer;  // Pointer to supported Super VGA modes
    U16 Memory;       // Number of 64kb memory blocks on board
} VESAINFOBLOCK, *LPVESAINFOBLOCK;

/************************************************************************/

typedef struct tag_MODEINFOBLOCK {
    U16 Attributes;
    U8 WindowAAttributes;
    U8 WindowBAttributes;
    U16 WindowGranularity;
    U16 WindowSize;
    U16 WindowAStartSegment;
    U16 WindowBStartSegment;
    U32 WindowFunctionPointer;
    U16 BytesPerScanLine;

    U16 XResolution;
    U16 YResolution;
    U8 XCharSize;
    U8 YCharSize;
    U8 NumberOfPlanes;
    U8 BitsPerPixel;
    U8 NumberOfBanks;
    U8 MemoryModel;
    U8 BankSizeKB;
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
} MODEINFOBLOCK, *LPMODEINFOBLOCK;

/************************************************************************/

typedef struct tag_VIDEOMODESPECS {
    U32 Mode;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    COLOR (*SetPixel)(LPVESA_CONTEXT, I32, I32, COLOR);
    COLOR (*GetPixel)(LPVESA_CONTEXT, I32, I32);
    U32 (*Line)(LPVESA_CONTEXT, I32, I32, I32, I32);
    U32 (*Rect)(LPVESA_CONTEXT, I32, I32, I32, I32);
} VIDEOMODESPECS, *LPVIDEOMODESPECS;

/************************************************************************/

struct tag_VESA_CONTEXT {
    GRAPHICSCONTEXT Header;
    VESAINFOBLOCK VESAInfo;
    MODEINFOBLOCK ModeInfo;
    VIDEOMODESPECS ModeSpecs;
    U32 PixelSize;
    PHYSICAL FrameBufferPhysical;
    LINEAR FrameBufferLinear;
    U32 FrameBufferSize;
    BOOL LinearFrameBufferEnabled;
};

/***************************************************************************/

VIDEOMODESPECS VESAModeSpecs[] = {
    {0x0100, 640, 400, 8, SetPixel8, GetPixel8, Line8, Rect8},
    {0x0101, 640, 480, 8, SetPixel8, GetPixel8, Line8, Rect8},
    {0x0103, 800, 600, 8, SetPixel8, GetPixel8, Line8, Rect8},
    {0x0105, 1024, 768, 8, SetPixel8, GetPixel8, Line8, Rect8},
    {0x0107, 1280, 1024, 8, SetPixel8, GetPixel8, Line8, Rect8},
    {0x010D, 320, 200, 16, SetPixel16, GetPixel16, Line16, Rect16},
    {0x010F, 320, 200, 24, SetPixel24, GetPixel24, Line24, Rect24},
    {0x0110, 640, 480, 16, SetPixel16, GetPixel16, Line16, Rect16},
    {0x0112, 640, 480, 24, SetPixel24, GetPixel24, Line24, Rect24},
    {0x0113, 800, 600, 16, SetPixel16, GetPixel16, Line16, Rect16},
    {0x0115, 800, 600, 24, SetPixel24, GetPixel24, Line24, Rect24},
    {0x0116, 1024, 768, 16, SetPixel16, GetPixel16, Line16, Rect16},
    {0x0118, 1024, 768, 24, SetPixel24, GetPixel24, Line24, Rect24},
    {0x0119, 1280, 1024, 16, SetPixel16, GetPixel16, Line16, Rect16},
    {0x011B, 1280, 1024, 24, SetPixel24, GetPixel24, Line24, Rect24},
    {0x0000, 0, 0, 0, NULL, NULL, NULL, NULL}};

/***************************************************************************/

#define VIDEO_CALL 0x10
#define VESA_LINEAR_FRAMEBUFFER_FLAG 0x4000

VESA_CONTEXT VESAContext = {
    .Header = {.TypeID = KOID_GRAPHICSCONTEXT, .References = 1, .Mutex = EMPTY_MUTEX, .Driver = &VESADriver}};

/***************************************************************************/

/**
 * @brief Initialize VESA context and retrieve controller info.
 *
 * Performs real-mode calls to fetch VESA data, seeds graphics context defaults,
 * and validates signature.
 *
 * @return TRUE on success, FALSE otherwise
 */
static BOOL InitializeVESA(void) {
    INTEL_X86_REGISTERS Regs;

    // TODO : Fix real mode call in x86-64
#if defined(__EXOS_ARCH_X86_64__)
    return TRUE;
#endif

    DEBUG(TEXT("[InitializeVESA] Enter"));

    //-------------------------------------
    // Initialize the context

    VESAContext = (VESA_CONTEXT){
        .Header =
            {.TypeID = KOID_GRAPHICSCONTEXT,
             .References = 1,
             .Mutex = EMPTY_MUTEX,
             .Driver = &VESADriver,
             .LoClip = {.X = 0, .Y = 0},
             .HiClip = {.X = 100, .Y = 100},
             .RasterOperation = ROP_SET},
        .ModeSpecs = {.SetPixel = SetPixel8, .GetPixel = GetPixel8, .Line = Line8, .Rect = Rect8}};

    //-------------------------------------
    // Get VESA general information

    MemorySet(&Regs, 0, sizeof(Regs));
    Regs.X.AX = 0x4F00;
    Regs.X.ES = (LOW_MEMORY_PAGE_6) >> MUL_16;
    Regs.X.DI = 0;

    RealModeCall(VIDEO_CALL, &Regs);

    DEBUG(TEXT("[InitializeVESA] Real mode call done"));

    if (Regs.X.AX == 0x004F) {
        MemoryCopy(&(VESAContext.VESAInfo), (LPVOID)(LOW_MEMORY_PAGE_6), sizeof(VESAINFOBLOCK));

        DEBUG(TEXT("[InitializeVESA] VESAInfo.Signature: %x %x %x %x"),
            VESAContext.VESAInfo.Signature[0], VESAContext.VESAInfo.Signature[1],
            VESAContext.VESAInfo.Signature[2], VESAContext.VESAInfo.Signature[3]);
        DEBUG(TEXT("[InitializeVESA] VESAInfo.Version: %u"), VESAContext.VESAInfo.Version);
        DEBUG(TEXT("[InitializeVESA] VESAInfo.Memory: %u KB"), VESAContext.VESAInfo.Memory * 64);

        if (VESAContext.VESAInfo.Signature[0] != 'V') return DF_RETURN_GENERIC;
        if (VESAContext.VESAInfo.Signature[1] != 'E') return DF_RETURN_GENERIC;
        if (VESAContext.VESAInfo.Signature[2] != 'S') return DF_RETURN_GENERIC;
        if (VESAContext.VESAInfo.Signature[3] != 'A') return DF_RETURN_GENERIC;
    } else {
        ERROR(TEXT("[InitializeVESA] Call to VESA information failed"));
    }

    /*
    KernelPrint("\n");
    KernelPrint("VESA driver version %d.%d\n", VER_MAJOR, VER_MINOR);
    KernelPrint("Manufacturer : %s\n",
                MKLINPTR(VESAContext.VESAInfo.OEMString));
    KernelPrint("Version      : %d\n", VESAContext.VESAInfo.Version);
    KernelPrint("Total memory : %d KB\n",
                (VESAContext.VESAInfo.Memory << MUL_64KB) >> MUL_1KB);
                */

    DEBUG(TEXT("[InitializeVESA] Exit"));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Tear down VESA resources and restore text mode.
 *
 * Unmaps LFB when mapped and switches back to BIOS text mode.
 *
 * @return DF_RETURN_SUCCESS always
 */
static U32 ShutdownVESA(void) {
    INTEL_X86_REGISTERS Regs;

    // TODO : Fix real mode call in x86-64
#if defined(__EXOS_ARCH_X86_64__)
    return TRUE;
#endif

    if (VESAContext.LinearFrameBufferEnabled != FALSE && VESAContext.FrameBufferLinear != 0 &&
        VESAContext.FrameBufferSize != 0) {
        UnMapIOMemory(VESAContext.FrameBufferLinear, VESAContext.FrameBufferSize);
    }
    VESAContext.LinearFrameBufferEnabled = FALSE;
    VESAContext.FrameBufferLinear = 0;
    VESAContext.FrameBufferSize = 0;
    VESAContext.FrameBufferPhysical = 0;
    VESAContext.Header.MemoryBase = NULL;

    //-------------------------------------
    // Set text mode

    MemorySet(&Regs, 0, sizeof(Regs));
    Regs.X.AX = 0x4F02;
    Regs.X.BX = 0x03;
    RealModeCall(VIDEO_CALL, &Regs);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Set a VESA video mode and map the linear frame buffer if available.
 *
 * Selects the requested resolution/depth, queries mode info, maps the LFB,
 * and updates graphics context capabilities.
 *
 * @param Info Requested mode description
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_GENERIC otherwise
 */
static U32 SetVideoMode(LPGRAPHICSMODEINFO Info) {
    INTEL_X86_REGISTERS Regs;
    U16* ModePtr = NULL;
    U32 Found = 0;
    U32 Index = 0;
    U32 Mode = 0;
    UINT FrameBufferSize = 0;
    LINEAR LinearBase = 0;

    DEBUG(TEXT("[SetVideoMode] GFX mode request : %ux%u"), Info->Width, Info->Height);

    if (VESAContext.LinearFrameBufferEnabled != FALSE && VESAContext.FrameBufferLinear != 0 &&
        VESAContext.FrameBufferSize != 0) {
        UnMapIOMemory(VESAContext.FrameBufferLinear, VESAContext.FrameBufferSize);
    }
    VESAContext.LinearFrameBufferEnabled = FALSE;
    VESAContext.FrameBufferLinear = 0;
    VESAContext.FrameBufferSize = 0;
    VESAContext.FrameBufferPhysical = 0;
    VESAContext.Header.MemoryBase = NULL;

    for (Index = 0;; Index++) {
        if (VESAModeSpecs[Index].Mode == 0) return DF_RETURN_GENERIC;

        DEBUG(TEXT("[SetVideoMode] Checking mode %x"), VESAModeSpecs[Index].Mode);

        if (VESAModeSpecs[Index].Width == Info->Width && VESAModeSpecs[Index].Height == Info->Height &&
            VESAModeSpecs[Index].BitsPerPixel == Info->BitsPerPixel) {
            BOOL ModeListed = FALSE;
            BOOL ModeListValid = TRUE;

            ModePtr = (U16*)(UINT)MKLINPTR(VESAContext.VESAInfo.ModePointer);

            DEBUG(TEXT("[SetVideoMode] Mode res = %ux%ux%u"), VESAModeSpecs[Index].Width,
                VESAModeSpecs[Index].Height, VESAModeSpecs[Index].BitsPerPixel);
            DEBUG(TEXT("[SetVideoMode] ModePtr = %p"), ModePtr);

            if (ModePtr == NULL || IsValidMemory((LINEAR)ModePtr) == FALSE) {
                ModeListValid = FALSE;
            } else {
                for (Mode = 0;; Mode++) {
                    LINEAR EntryAddress = (LINEAR)(ModePtr + Mode);

                    if (IsValidMemory(EntryAddress) == FALSE ||
                        IsValidMemory(EntryAddress + sizeof(U16) - 1) == FALSE) {
                        ModeListValid = FALSE;
                        break;
                    }

                    if (ModePtr[Mode] == 0xFFFF) break;

                    if (ModePtr[Mode] == VESAModeSpecs[Index].Mode) {
                        ModeListed = TRUE;
                        DEBUG(TEXT("[SetVideoMode] Mode found"));
                        break;
                    }
                }
            }

            MemoryCopy(&(VESAContext.ModeSpecs), VESAModeSpecs + Index, sizeof(VIDEOMODESPECS));
            Found = 1;

            if (ModeListed == FALSE) {
                if (ModeListValid == FALSE) {
                    WARNING(TEXT("[SetVideoMode] Mode list pointer invalid, forcing mode %x"), VESAModeSpecs[Index].Mode);
                } else {
                    WARNING(TEXT("[SetVideoMode] Mode %x not advertised, forcing selection"), VESAModeSpecs[Index].Mode);
                }
            }

            break;
        }
    }

    if (Found == 0) return DF_RETURN_GENERIC;

    //-------------------------------------
    // Get info about the mode

    DEBUG(TEXT("[SetVideoMode] Getting mode info..."));

    MemorySet(&Regs, 0, sizeof(Regs));
    Regs.X.AX = 0x4F01;
    Regs.X.CX = VESAContext.ModeSpecs.Mode;
    Regs.X.ES = (LOW_MEMORY_PAGE_6) >> MUL_16;
    Regs.X.DI = 0;
    RealModeCall(VIDEO_CALL, &Regs);

    if (Regs.X.AX != 0x004F) {
        ERROR(TEXT("[SetVideoMode] VESA GetModeInfo failed (AX=%x)"), Regs.X.AX);
        return DF_RETURN_GENERIC;
    }

    MemoryCopy(&(VESAContext.ModeInfo), (LPVOID)(LOW_MEMORY_PAGE_6), sizeof(MODEINFOBLOCK));

    if ((VESAContext.ModeInfo.Attributes & 0x80) == 0) {
        ERROR(TEXT("[SetVideoMode] Mode %x does not support linear frame buffers"), VESAContext.ModeSpecs.Mode);
        return DF_RETURN_GENERIC;
    }

    if (VESAContext.ModeInfo.PhysBasePtr == 0) {
        ERROR(TEXT("[SetVideoMode] Mode %x returned null PhysBasePtr"), VESAContext.ModeSpecs.Mode);
        return DF_RETURN_GENERIC;
    }

    //-------------------------------------
    // Set the mode

    MemorySet(&Regs, 0, sizeof(Regs));
    Regs.X.AX = 0x4F02;
    Regs.X.BX = VESAContext.ModeSpecs.Mode | VESA_LINEAR_FRAMEBUFFER_FLAG;
    RealModeCall(VIDEO_CALL, &Regs);

    if (Regs.X.AX != 0x004F) {
        ERROR(TEXT("[SetVideoMode] Failed to set mode %x (AX=%x)"), VESAContext.ModeSpecs.Mode, Regs.X.AX);
        return DF_RETURN_GENERIC;
    }

    //-------------------------------------
    // Set some attributes

    VESAContext.Header.Width = VESAContext.ModeSpecs.Width;
    VESAContext.Header.Height = VESAContext.ModeSpecs.Height;
    VESAContext.Header.BitsPerPixel = VESAContext.ModeSpecs.BitsPerPixel;
    VESAContext.PixelSize = VESAContext.Header.BitsPerPixel >> MUL_8;
    VESAContext.Header.LoClip.X = 0;
    VESAContext.Header.LoClip.Y = 0;
    VESAContext.Header.HiClip.X = VESAContext.Header.Width - 1;
    VESAContext.Header.HiClip.Y = VESAContext.Header.Height - 1;
    VESAContext.Header.BytesPerScanLine = VESAContext.ModeInfo.BytesPerScanLine;
    if (VESAContext.Header.BytesPerScanLine == 0) {
        VESAContext.Header.BytesPerScanLine = VESAContext.Header.Width * VESAContext.PixelSize;
    }

    FrameBufferSize = VESAContext.Header.BytesPerScanLine * VESAContext.Header.Height;
    if (FrameBufferSize == 0) {
        ERROR(TEXT("[SetVideoMode] Frame buffer size is zero (pitch=%u height=%u)"), VESAContext.Header.BytesPerScanLine,
            VESAContext.Header.Height);
        return DF_RETURN_GENERIC;
    }

    VESAContext.FrameBufferPhysical = (PHYSICAL)VESAContext.ModeInfo.PhysBasePtr;
    LinearBase = MapIOMemory(VESAContext.FrameBufferPhysical, FrameBufferSize);
    if (LinearBase == 0) {
        ERROR(TEXT("[SetVideoMode] MapIOMemory failed for LFB base %p size %u"),
            (LPVOID)(LINEAR)VESAContext.FrameBufferPhysical, FrameBufferSize);
        VESAContext.FrameBufferPhysical = 0;
        return DF_RETURN_GENERIC;
    }

    VESAContext.FrameBufferLinear = LinearBase;
    VESAContext.FrameBufferSize = FrameBufferSize;
    VESAContext.LinearFrameBufferEnabled = TRUE;
    VESAContext.Header.MemoryBase = (U8*)(LINEAR)LinearBase;

    DEBUG(TEXT("[SetVideoMode] LFB mapped at %p (phys=%p pitch=%u size=%u)"), VESAContext.Header.MemoryBase,
        (LPVOID)(LINEAR)VESAContext.FrameBufferPhysical, VESAContext.Header.BytesPerScanLine, FrameBufferSize);

#if VESA_ENABLE_SELFTEST
    VESADrawSelfTest(&VESAContext);
#endif

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/*
static U32 SetClip(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Temp;

    Context->Header.LoClip.X = X1;
    Context->Header.LoClip.Y = Y1;
    Context->Header.HiClip.X = X2;
    Context->Header.HiClip.Y = Y2;

    CLIPVALUE(Context->Header.LoClip.X, 0, Context->Header.Width - 1);
    CLIPVALUE(Context->Header.LoClip.Y, 0, Context->Header.Height - 1);
    CLIPVALUE(Context->Header.HiClip.X, 0, Context->Header.Width - 1);
    CLIPVALUE(Context->Header.HiClip.Y, 0, Context->Header.Height - 1);

    if (Context->Header.LoClip.X > Context->Header.HiClip.X) {
        Temp = Context->Header.LoClip.X;
        Context->Header.LoClip.X = Context->Header.HiClip.X;
        Context->Header.HiClip.X = Temp;
    }

    if (Context->Header.LoClip.Y > Context->Header.HiClip.Y) {
        Temp = Context->Header.LoClip.Y;
        Context->Header.LoClip.Y = Context->Header.HiClip.Y;
        Context->Header.HiClip.Y = Temp;
    }

    return 0;
}
*/

/***************************************************************************/

/**
 * @brief Write an 8bpp pixel using current raster operation.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @param Color 8-bit color value
 * @return Previous pixel value
 */
static COLOR SetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Plane = Context->Header.MemoryBase + Offset;
    OldColor = *Plane;

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            *Plane = (U8)Color;
        } break;

        case ROP_XOR: {
            *Plane ^= (U8)Color;
        } break;

        case ROP_OR: {
            *Plane |= (U8)Color;
        } break;

        case ROP_AND: {
            *Plane &= (U8)Color;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Write a 16bpp pixel using current raster operation.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @param Color 16-bit color value
 * @return Previous pixel value
 */
static COLOR SetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Plane = Context->Header.MemoryBase + Offset;
    OldColor = *((U16*)Plane);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            *((U16*)Plane) = (U16)Color;
        } break;

        case ROP_XOR: {
            *((U16*)Plane) ^= (U16)Color;
        } break;

        case ROP_OR: {
            *((U16*)Plane) |= (U16)Color;
        } break;

        case ROP_AND: {
            *((U16*)Plane) &= (U16)Color;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Write a 24bpp pixel using current raster operation.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @param Color 24-bit packed RGB color
 * @return Previous pixel value
 */
static COLOR SetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Offset;
    U8* Pixel;
    U8 R;
    U8 G;
    U8 B;
    U32 Converted;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X * 3);
    Pixel = Context->Header.MemoryBase + Offset;

    Converted = 0;
    Converted |= (((Color >> 0) & 0xFF) << 16);
    Converted |= (((Color >> 8) & 0xFF) << 8);
    Converted |= (((Color >> 16) & 0xFF) << 0);

    R = (U8)((Converted >> 0) & 0xFF);
    G = (U8)((Converted >> 8) & 0xFF);
    B = (U8)((Converted >> 16) & 0xFF);

    OldColor = (U32)Pixel[0] | ((U32)Pixel[1] << 8) | ((U32)Pixel[2] << 16);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            Pixel[0] = R;
            Pixel[1] = G;
            Pixel[2] = B;
        } break;

        case ROP_XOR: {
            Pixel[0] ^= R;
            Pixel[1] ^= G;
            Pixel[2] ^= B;
        } break;

        case ROP_OR: {
            Pixel[0] |= R;
            Pixel[1] |= G;
            Pixel[2] |= B;
        } break;

        case ROP_AND: {
            Pixel[0] &= R;
            Pixel[1] &= G;
            Pixel[2] &= B;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

/**
 * @brief Read an 8bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 8-bit color value or 0 if out of clip
 */
static COLOR GetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Color = *((U8*)(Context->Header.MemoryBase + Offset));

    return Color;
}

/***************************************************************************/

/**
 * @brief Read a 16bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 16-bit color value or 0 if out of clip
 */
static COLOR GetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Color = *((U16*)(Context->Header.MemoryBase + Offset));

    return Color;
}

/***************************************************************************/

/**
 * @brief Read a 24bpp pixel.
 *
 * @param Context VESA context holding frame buffer info
 * @param X Horizontal coordinate
 * @param Y Vertical coordinate
 * @return 24-bit packed RGB color or 0 if out of clip
 */
static COLOR GetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U8* Pixel;
    U32 Offset;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X || Y < Context->Header.LoClip.Y ||
        Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + (X * 3);
    Pixel = Context->Header.MemoryBase + Offset;

    Color = (U32)Pixel[0];
    Color |= (U32)Pixel[1] << 8;
    Color |= (U32)Pixel[2] << 16;

    return Color;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 8bpp mode (stub).
 *
 * Parameters are unused; kept for interface parity.
 *
 * @return Always 0
 */
static U32 Line8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    UNUSED(Context);
    UNUSED(X1);
    UNUSED(Y1);
    UNUSED(X2);
    UNUSED(Y2);
    /*
      word d, dx, dy, ai, bi, xi, yi;
      word line_bit = 0;
      word x1 = Context.CurrentX;
      word y1 = Context.CurrentY;
      word x2 = x;
      word y2 = y;

      if (x1 < x2) { xi = 1; dx = x2 - x1; } else { xi = -1; dx = x1 - x2; }
      if (y1 < y2) { yi = 1; dy = y2 - y1; } else { yi = -1; dy = y1 - y2; }

      if (Context->DrawStyle != 0xFFFF)
      {
    if ((Context->DrawStyle >> line_bit) & 1) setpix1(x1, y1);
    line_bit++;

    if (dx > dy)
    {
      ai = (dy - dx) * 2;
      bi = dy * 2;
      d  = bi - dx;
      while (x1 != x2)
      {
        if (d >= 0)
        {
          y1 += yi;
          d += ai;
        }
        else
        {
          d += bi;
        }
        x1 += xi;
        if ((Context->DrawStyle >> line_bit) & 1) setpix1(x1, y1);
        line_bit++;
        if (line_bit > 15) line_bit = 0;
      }
    }
    else
    {
      ai = (dx - dy) * 2;
      bi = dx * 2;
      d  = bi - dy;
      while (y1 != y2)
      {
        if (d >= 0)
        {
          x1 += xi;
          d  += ai;
        }
        else
        {
          d += bi;
        }
        y1 += yi;
        if ((Context->DrawStyle >> line_bit) & 1) setpix1(x1, y1);
        line_bit++;
        if (line_bit > 15) line_bit = 0;
      }
    }
      }
      else
      {
    setpix1(x1, y1);
    if (dx>dy)
    {
      ai=(dy-dx)*2; bi=dy*2; d=bi-dx;
      while (x1!=x2)
      {
        if (d>=0) { y1+=yi; d+=ai; } else { d+=bi; }
        x1+=xi; setpix1(x1, y1);
      }
    }
    else
    {
      ai=(dx-dy)*2; bi=dx*2; d=bi-dy;
      while (y1!=y2)
      {
        if (d>=0) { x1+=xi; d+=ai; } else { d+=bi; }
        y1+=yi; setpix1(x1, y1);
      }
    }
      }
    */

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 16bpp mode.
 *
 * @param Context VESA context with pen state
 * @param X1 Start X
 * @param Y1 Start Y
 * @param X2 End X
 * @param Y2 End Y
 * @return 0 on success, MAX_U32 on invalid pen
 */
static U32 Line16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 d, dx, dy, ai, bi, xi, yi;
    U32 LineBit = 0;
    U32 Pattern;
    COLOR Color;

    if (Context->Header.Pen == NULL) return MAX_U32;
    if (Context->Header.Pen->TypeID != KOID_PEN) return MAX_U32;

    Color = Context->Header.Pen->Color;
    Pattern = Context->Header.Pen->Pattern;

    if (X1 < X2) {
        xi = 1;
        dx = X2 - X1;
    } else {
        xi = -1;
        dx = X1 - X2;
    }
    if (Y1 < Y2) {
        yi = 1;
        dy = Y2 - Y1;
    } else {
        yi = -1;
        dy = Y1 - Y2;
    }

    if ((Pattern >> LineBit) & 1) {
        Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
    }
    LineBit++;

    if (dx > dy) {
        ai = (dy - dx) * 2;
        bi = dy * 2;
        d = bi - dx;
        while (X1 != X2) {
            if (d >= 0) {
                Y1 += yi;
                d += ai;
            } else {
                d += bi;
            }
            X1 += xi;
            if ((Pattern >> LineBit) & 1) {
                Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
            }
            LineBit++;
            if (LineBit > 31) LineBit = 0;
        }
    } else {
        ai = (dx - dy) * 2;
        bi = dx * 2;
        d = bi - dy;
        while (Y1 != Y2) {
            if (d >= 0) {
                X1 += xi;
                d += ai;
            } else {
                d += bi;
            }
            Y1 += yi;
            if ((Pattern >> LineBit) & 1) {
                Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
            }
            LineBit++;
            if (LineBit > 31) LineBit = 0;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw a patterned line in 24bpp mode.
 *
 * @param Context VESA context with pen state
 * @param X1 Start X
 * @param Y1 Start Y
 * @param X2 End X
 * @param Y2 End Y
 * @return 0 on success, MAX_U32 on invalid pen
 */
static U32 Line24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 d, dx, dy, ai, bi, xi, yi;
    U32 LineBit = 0;
    U32 Pattern;
    COLOR Color;

    if (Context->Header.Pen == NULL) return MAX_U32;
    if (Context->Header.Pen->TypeID != KOID_PEN) return MAX_U32;

    Color = Context->Header.Pen->Color;
    Pattern = Context->Header.Pen->Pattern;

    Color = 0;
    Color |= (((Context->Header.Pen->Color >> 0) & 0xFF) << 16);
    Color |= (((Context->Header.Pen->Color >> 8) & 0xFF) << 8);
    Color |= (((Context->Header.Pen->Color >> 16) & 0xFF) << 0);

    if (X1 < X2) {
        xi = 1;
        dx = X2 - X1;
    } else {
        xi = -1;
        dx = X1 - X2;
    }
    if (Y1 < Y2) {
        yi = 1;
        dy = Y2 - Y1;
    } else {
        yi = -1;
        dy = Y1 - Y2;
    }

    if ((Pattern >> LineBit) & 1) {
        Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
    }
    LineBit++;

    if (dx > dy) {
        ai = (dy - dx) * 2;
        bi = dy * 2;
        d = bi - dx;
        while (X1 != X2) {
            if (d >= 0) {
                Y1 += yi;
                d += ai;
            } else {
                d += bi;
            }
            X1 += xi;
            if ((Pattern >> LineBit) & 1) {
                Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
            }
            LineBit++;
            if (LineBit > 31) LineBit = 0;
        }
    } else {
        ai = (dx - dy) * 2;
        bi = dx * 2;
        d = bi - dy;
        while (Y1 != Y2) {
            if (d >= 0) {
                X1 += xi;
                d += ai;
            } else {
                d += bi;
            }
            Y1 += yi;
            if ((Pattern >> LineBit) & 1) {
                Context->ModeSpecs.SetPixel(Context, X1, Y1, Color);
            }
            LineBit++;
            if (LineBit > 31) LineBit = 0;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Fill rectangle in 8bpp mode (stub).
 *
 * Parameters are unused; kept for interface parity.
 *
 * @return Always 0
 */
static U32 Rect8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    UNUSED(Context);
    UNUSED(X1);
    UNUSED(Y1);
    UNUSED(X2);
    UNUSED(Y2);
    /*
      long  offset;
      long  bank;
      word  x, y;
      word  tmp;
      word  line_bit=0;
      ubyte *plane;

      if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
      if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

      if (mode == _GBORDER)
      {
    _moveto(x1, y1);
    _lineto(x2, y1);
    _lineto(x2, y2);
    _lineto(x1, y2);
    _lineto(x1, y1);
      }
      else
      if (mode == _GFILLINTERIOR)
      {
    if (x1 < Context->Header.LoClip.X) x1 = Context->Header.LoClip.X;
    else
    if (x1 > Context->Header.HiClip.X) x1 = Context->Header.HiClip.X;
    if (x2 < Context->Header.LoClip.X) x2 = Context->Header.LoClip.X;
    else
    if (x2 > Context->Header.HiClip.X) x2 = Context->Header.HiClip.X;
    if (y1 < Context->Header.LoClip.Y) y1 = Context->Header.LoClip.Y;
    else
    if (y1 > Context->Header.HiClip.Y) y1 = Context->Header.HiClip.Y;
    if (y2 < Context->Header.LoClip.Y) y2 = Context->Header.LoClip.Y;
    else
    if (y2 > Context->Header.HiClip.Y) y2 = Context->Header.HiClip.Y;

    offset = (y1 * Context->Header.BytesPerScanLine) + x1;
    bank   = offset >> Context->GranularShift;

    switch_bank();

    for (y = y1; y <= y2; y++)
    {
      plane = Context->Header.MemoryBase + (offset &
      Context->GranularModulo);

      for (x = x1; x <= x2; x++)
      {
        switch (Context->Header.RasterOperation)
        {
          case _GPSET :
          {
        *plane = (U8) Context->CurrentColor;
          }
          break;
          case _GXOR :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor ^ (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
          case _GOR :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor | (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
          case _GAND :
          {
        pcolor = (*plane);
        pcolor = (U8) Context->CurrentColor & (U8) pcolor;
        *plane = (U8) pcolor;
          }
          break;
        }
        plane++;
      }

      offset += Context->Header.BytesPerScanLine;
      bank = offset >> Context->GranularShift;
      switch_bank();
    }
      }
    */

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw filled and/or outlined rectangle in 16bpp mode.
 *
 * Uses brush for fill and pen for border when provided.
 *
 * @param Context VESA context
 * @param X1 Left coordinate
 * @param Y1 Top coordinate
 * @param X2 Right coordinate
 * @param Y2 Bottom coordinate
 * @return 0 on completion
 */
static U32 Rect16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 X, Y;
    U32 Temp;
    U32 Color;

    if (X1 > X2) {
        Temp = X1;
        X1 = X2;
        X2 = Temp;
    }
    if (Y1 > Y2) {
        Temp = Y1;
        Y1 = Y2;
        Y2 = Temp;
    }

    if (Context->Header.Brush != NULL && Context->Header.Brush->TypeID == KOID_BRUSH) {
        Color = Context->Header.Brush->Color;

        for (Y = Y1; Y <= Y2; Y++) {
            for (X = X1; X <= X2; X++) {
                Context->ModeSpecs.SetPixel(Context, X, Y, Color);
            }
        }
    }

    if (Context->Header.Pen != NULL && Context->Header.Pen->TypeID == KOID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw filled and/or outlined rectangle in 24bpp mode.
 *
 * Uses brush for fill and pen for border when provided.
 *
 * @param Context VESA context
 * @param X1 Left coordinate
 * @param Y1 Top coordinate
 * @param X2 Right coordinate
 * @param Y2 Bottom coordinate
 * @return 0 on completion
 */
static U32 Rect24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 X;
    I32 Y;
    U32 Temp;
    U32 ConvertedColor;
    U8 R = 0;
    U8 G = 0;
    U8 B = 0;
    UINT Pitch;

    if (X1 > X2) {
        Temp = X1;
        X1 = X2;
        X2 = Temp;
    }
    if (Y1 > Y2) {
        Temp = Y1;
        Y1 = Y2;
        Y2 = Temp;
    }

    Pitch = Context->Header.BytesPerScanLine;

    if (Context->Header.Brush != NULL && Context->Header.Brush->TypeID == KOID_BRUSH) {
        ConvertedColor = 0;
        ConvertedColor |= (((Context->Header.Brush->Color >> 0) & 0xFF) << 16);
        ConvertedColor |= (((Context->Header.Brush->Color >> 8) & 0xFF) << 8);
        ConvertedColor |= (((Context->Header.Brush->Color >> 16) & 0xFF) << 0);

        R = (U8)((ConvertedColor >> 0) & 0xFF);
        G = (U8)((ConvertedColor >> 8) & 0xFF);
        B = (U8)((ConvertedColor >> 16) & 0xFF);

        if (X1 < Context->Header.LoClip.X) X1 = Context->Header.LoClip.X;
        if (X1 > Context->Header.HiClip.X) X1 = Context->Header.HiClip.X;
        if (X2 < Context->Header.LoClip.X) X2 = Context->Header.LoClip.X;
        if (X2 > Context->Header.HiClip.X) X2 = Context->Header.HiClip.X;
        if (Y1 < Context->Header.LoClip.Y) Y1 = Context->Header.LoClip.Y;
        if (Y1 > Context->Header.HiClip.Y) Y1 = Context->Header.HiClip.Y;
        if (Y2 < Context->Header.LoClip.Y) Y2 = Context->Header.LoClip.Y;
        if (Y2 > Context->Header.HiClip.Y) Y2 = Context->Header.HiClip.Y;

        for (Y = Y1; Y <= Y2; Y++) {
            U8* Pixel = Context->Header.MemoryBase + (Y * Pitch) + (X1 * 3);

            for (X = X1; X <= X2; X++) {
                switch (Context->Header.RasterOperation) {
                    case ROP_SET: {
                        Pixel[0] = R;
                        Pixel[1] = G;
                        Pixel[2] = B;
                    } break;

                    case ROP_XOR: {
                        Pixel[0] ^= R;
                        Pixel[1] ^= G;
                        Pixel[2] ^= B;
                    } break;

                    case ROP_OR: {
                        Pixel[0] |= R;
                        Pixel[1] |= G;
                        Pixel[2] |= B;
                    } break;

                    case ROP_AND: {
                        Pixel[0] &= R;
                        Pixel[1] &= G;
                        Pixel[2] &= B;
                    } break;
                }

                Pixel += 3;
            }
        }
    }

    // Draw borders

    if (Context->Header.Pen != NULL && Context->Header.Pen->TypeID == KOID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Draw a simple self-test pattern for sanity checks.
 *
 * Renders colored horizontal bands in the top portion of the frame buffer.
 *
 * @param Context VESA context
 */
static void VESADrawSelfTest(LPVESA_CONTEXT Context) {
    static const COLOR Colors[] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00};
    const I32 NumBands = (I32)(sizeof(Colors) / sizeof(Colors[0]));
    COLOR (*SetPixel)(LPVESA_CONTEXT, I32, I32, COLOR);
    I32 Width;
    I32 Height;
    I32 StripeWidth;
    I32 TestHeight;
    I32 Index;
    I32 X;
    I32 Y;
    I32 X1;
    I32 X2;

    SetPixel = Context->ModeSpecs.SetPixel;
    if (SetPixel == NULL) return;

    Width = (I32)Context->Header.Width;
    Height = (I32)Context->Header.Height;
    if (Width <= 0 || Height <= 0) return;

    StripeWidth = Width / NumBands;
    if (StripeWidth <= 0) StripeWidth = Width;

    TestHeight = Height / 16;
    if (TestHeight < 16) TestHeight = Height;
    if (TestHeight > Height) TestHeight = Height;

    DEBUG(TEXT("[VESADrawSelfTest] Drawing %u color bands (%ux%u test area)"), (U32)NumBands, (U32)Width, (U32)TestHeight);

    for (Index = 0; Index < NumBands; Index++) {
        X1 = Index * StripeWidth;
        X2 = X1 + StripeWidth - 1;

        if (Index == NumBands - 1) X2 = Width - 1;
        if (X2 >= Width) X2 = Width - 1;
        if (X1 < 0) X1 = 0;

        for (Y = 0; Y < TestHeight; Y++) {
            for (X = X1; X <= X2; X++) {
                SetPixel(Context, X, Y, Colors[Index]);
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Create a brush object from descriptor.
 *
 * @param Info Brush creation parameters
 * @return Allocated brush or NULL on failure
 */
static LPBRUSH VESA_CreateBrush(LPBRUSHINFO Info) {
    LPBRUSH Brush;

    if (Info == NULL) return NULL;

    Brush = (LPBRUSH)KernelHeapAlloc(sizeof(BRUSH));
    if (Brush == NULL) return NULL;

    MemorySet(Brush, 0, sizeof(BRUSH));

    Brush->TypeID = KOID_BRUSH;
    Brush->References = 1;
    Brush->Color = Info->Color;
    Brush->Pattern = Info->Pattern;

    return Brush;
}

/***************************************************************************/

/**
 * @brief Create a pen object from descriptor.
 *
 * @param Info Pen creation parameters
 * @return Allocated pen or NULL on failure
 */
static LPPEN VESA_CreatePen(LPPENINFO Info) {
    LPPEN Pen;

    if (Info == NULL) return NULL;

    Pen = (LPPEN)KernelHeapAlloc(sizeof(PEN));
    if (Pen == NULL) return NULL;

    MemorySet(Pen, 0, sizeof(PEN));

    Pen->TypeID = KOID_BRUSH;
    Pen->References = 1;
    Pen->Color = Info->Color;
    Pen->Pattern = Info->Pattern;

    return Pen;
}

/***************************************************************************/

/**
 * @brief Set a pixel via driver interface with mutex protection.
 *
 * @param Info Pixel operation descriptor
 * @return 1 on success, 0 on failure
 */
static U32 VESA_SetPixel(LPPIXELINFO Info) {
    LPVESA_CONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESA_CONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.TypeID != KOID_GRAPHICSCONTEXT) return 0;

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Info->Color = Context->ModeSpecs.SetPixel(Context, Info->X, Info->Y, Info->Color);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

/**
 * @brief Get a pixel via driver interface with mutex protection.
 *
 * @param Info Pixel operation descriptor
 * @return 1 on success, 0 on failure
 */
static U32 VESA_GetPixel(LPPIXELINFO Info) {
    LPVESA_CONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESA_CONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.TypeID != KOID_GRAPHICSCONTEXT) return 0;

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Info->Color = Context->ModeSpecs.GetPixel(Context, Info->X, Info->Y);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

/**
 * @brief Draw a line via driver interface.
 *
 * @param Info Line descriptor
 * @return 1 on success, 0 on failure
 */
static U32 VESA_Line(LPLINEINFO Info) {
    LPVESA_CONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESA_CONTEXT)Info->GC;

    // if (Context == NULL) return 0;
    if (Context == NULL) Context = &VESAContext;
    if (Context->Header.TypeID != KOID_GRAPHICSCONTEXT) return 0;

    // LockMutex(&(Context->Header.Mutex), INFINITY);

    Context->ModeSpecs.Line(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);

    // UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

/**
 * @brief Draw a rectangle via driver interface.
 *
 * Applies fill and border according to current brush/pen.
 *
 * @param Info Rectangle descriptor
 * @return 1 on success, 0 on failure
 */
static U32 VESA_Rectangle(LPRECTINFO Info) {
    LPVESA_CONTEXT Context;
    static U32 DATA_SECTION VESARectangleDebugCount = 0;

    if (Info == NULL) return 0;

    Context = (LPVESA_CONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.TypeID != KOID_GRAPHICSCONTEXT) return 0;

#if VESA_ENABLE_SELFTEST
    if (VESARectangleLogCount < 16) {
        LPBRUSH Brush = Context->Header.Brush;
        LPPEN Pen = Context->Header.Pen;
        UNUSED(Brush);
        UNUSED(Pen);
        VESARectangleLogCount++;
    }
#endif

    if (VESARectangleDebugCount < 32) {
        VESARectangleDebugCount++;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Context->ModeSpecs.Rect(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

/**
 * @brief Driver command dispatcher for VESA graphics.
 *
 * Handles load/unload, mode setting, drawing primitives, and resource creation.
 *
 * @param Function Driver function code
 * @param Param Function-specific parameter
 * @return Function-specific status code
 */
UINT VESACommands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            if ((VESADriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeVESA()) {
                VESADriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;
        case DF_UNLOAD:
            if ((VESADriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ShutdownVESA();
            VESADriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_GFX_GETMODEINFO: {
            LPGRAPHICSMODEINFO Info = (LPGRAPHICSMODEINFO)Param;
            SAFE_USE(Info) {
                Info->Width = VESAContext.Header.Width;
                Info->Height = VESAContext.Header.Height;
                Info->BitsPerPixel = VESAContext.Header.BitsPerPixel;
                return DF_RETURN_SUCCESS;
            }
            return DF_RETURN_GENERIC;
        }
        case DF_GFX_SETMODE:
            return SetVideoMode((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_CREATEBRUSH:
            return (UINT)VESA_CreateBrush((LPBRUSHINFO)Param);
        case DF_GFX_CREATEPEN:
            return (UINT)VESA_CreatePen((LPPENINFO)Param);
        case DF_GFX_SETPIXEL:
            return VESA_SetPixel((LPPIXELINFO)Param);
        case DF_GFX_GETPIXEL:
            return VESA_GetPixel((LPPIXELINFO)Param);
        case DF_GFX_LINE:
            return VESA_Line((LPLINEINFO)Param);
        case DF_GFX_RECTANGLE:
            return VESA_Rectangle((LPRECTINFO)Param);
        case DF_GFX_GETCAPABILITIES:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_PRESENT:
        case DF_GFX_WAITVBLANK:
        case DF_GFX_ALLOCSURFACE:
        case DF_GFX_FREESURFACE:
        case DF_GFX_SETSCANOUT:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
