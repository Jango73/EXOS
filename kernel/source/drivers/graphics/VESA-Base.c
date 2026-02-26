
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
#include "drivers/graphics/VESA-Shared.h"
#include "drivers/graphics/Graphics-TextRenderer.h"

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

static U32 DATA_SECTION VESARectangleLogCount = 0;

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
 * @brief Draw one text cell in VESA framebuffer.
 * @param Info Text cell descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VESA_TextPutCell(LPGFX_TEXT_CELL_INFO Info) {
    LPVESA_CONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPVESA_CONTEXT)Info->GC;
    if (Context == NULL || Context->Header.TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);
    Result = GfxTextPutCell((LPGRAPHICSCONTEXT)&Context->Header, Info);
    UnlockMutex(&(Context->Header.Mutex));
    return Result ? 1 : 0;
}

/***************************************************************************/

/**
 * @brief Clear one text region in VESA framebuffer.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VESA_TextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPVESA_CONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPVESA_CONTEXT)Info->GC;
    if (Context == NULL || Context->Header.TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);
    Result = GfxTextClearRegion((LPGRAPHICSCONTEXT)&Context->Header, Info);
    UnlockMutex(&(Context->Header.Mutex));
    return Result ? 1 : 0;
}

/***************************************************************************/

/**
 * @brief Scroll one text region in VESA framebuffer.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VESA_TextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPVESA_CONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPVESA_CONTEXT)Info->GC;
    if (Context == NULL || Context->Header.TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);
    Result = GfxTextScrollRegion((LPGRAPHICSCONTEXT)&Context->Header, Info);
    UnlockMutex(&(Context->Header.Mutex));
    return Result ? 1 : 0;
}

/***************************************************************************/

/**
 * @brief Draw cursor in VESA framebuffer.
 * @param Info Cursor descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VESA_TextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
    LPVESA_CONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPVESA_CONTEXT)Info->GC;
    if (Context == NULL || Context->Header.TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);
    Result = GfxTextSetCursor((LPGRAPHICSCONTEXT)&Context->Header, Info);
    UnlockMutex(&(Context->Header.Mutex));
    return Result ? 1 : 0;
}

/***************************************************************************/

/**
 * @brief Set cursor visibility in VESA backend.
 * @param Info Cursor visibility descriptor.
 * @return 1 on success, 0 on failure.
 */
static U32 VESA_TextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    LPVESA_CONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPVESA_CONTEXT)Info->GC;
    if (Context == NULL || Context->Header.TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Header.Mutex), INFINITY);
    Result = GfxTextSetCursorVisible((LPGRAPHICSCONTEXT)&Context->Header, Info);
    UnlockMutex(&(Context->Header.Mutex));
    return Result ? 1 : 0;
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
        case DF_GFX_CREATECONTEXT:
            return (UINT)(LPVOID)&VESAContext;
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
        case DF_GFX_TEXT_PUTCELL:
            return VESA_TextPutCell((LPGFX_TEXT_CELL_INFO)Param);
        case DF_GFX_TEXT_CLEAR_REGION:
            return VESA_TextClearRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SCROLL_REGION:
            return VESA_TextScrollRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR:
            return VESA_TextSetCursor((LPGFX_TEXT_CURSOR_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
            return VESA_TextSetCursorVisible((LPGFX_TEXT_CURSOR_VISIBLE_INFO)Param);
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
