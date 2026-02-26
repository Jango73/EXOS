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


    UEFI GOP graphics backend

\************************************************************************/

#include "GFX.h"
#include "Console.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/graphics/Graphics-TextRenderer.h"
#include "vbr-multiboot.h"

/************************************************************************/

#define GOP_GFX_VER_MAJOR 1
#define GOP_GFX_VER_MINOR 0

/************************************************************************/

typedef struct tag_GOP_GFX_STATE {
    PHYSICAL FrameBufferPhysical;
    LINEAR FrameBufferLinear;
    U32 FrameBufferSize;
    GRAPHICSCONTEXT Context;
    GFX_CAPABILITIES Capabilities;
} GOP_GFX_STATE, *LPGOP_GFX_STATE;

/************************************************************************/

static UINT GOPGfxCommands(UINT Function, UINT Param);

static DRIVER DATA_SECTION GOPGfxDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = GOP_GFX_VER_MAJOR,
    .VersionMinor = GOP_GFX_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "UEFI",
    .Product = "Graphics Output Protocol",
    .Flags = 0,
    .Command = GOPGfxCommands
};

static GOP_GFX_STATE DATA_SECTION GOPGfxState = {0};

/************************************************************************/

/**
 * @brief Retrieve GOP graphics driver descriptor.
 * @return Pointer to GOP graphics driver.
 */
LPDRIVER GOPGetDriver(void) {
    return &GOPGfxDriver;
}

/************************************************************************/

/**
 * @brief Write a single pixel into active GOP scanout.
 * @param Context Active graphics context.
 * @param X Horizontal coordinate.
 * @param Y Vertical coordinate.
 * @param Color In/out color value.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL GOPGfxWritePixel(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR* Color) {
    U32 Offset = 0;
    U8* Pixel = NULL;
    COLOR Previous = 0;

    if (Context == NULL || Color == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (X < Context->LoClip.X || X > Context->HiClip.X || Y < Context->LoClip.Y || Y > Context->HiClip.Y) {
        return FALSE;
    }

    switch (Context->BitsPerPixel) {
        case 32:
            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 2);
            Pixel = Context->MemoryBase + Offset;
            Previous = *((U32*)Pixel);

            switch (Context->RasterOperation) {
                case ROP_SET:
                    *((U32*)Pixel) = *Color;
                    break;
                case ROP_XOR:
                    *((U32*)Pixel) ^= *Color;
                    break;
                case ROP_OR:
                    *((U32*)Pixel) |= *Color;
                    break;
                case ROP_AND:
                    *((U32*)Pixel) &= *Color;
                    break;
                default:
                    return FALSE;
            }

            *Color = Previous;
            return TRUE;

        case 24: {
            U32 Converted = 0;
            U8 Red = 0;
            U8 Green = 0;
            U8 Blue = 0;

            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X * 3);
            Pixel = Context->MemoryBase + Offset;
            Previous = (U32)Pixel[0] | ((U32)Pixel[1] << 8) | ((U32)Pixel[2] << 16);

            Converted = (((*Color >> 0) & 0xFF) << 16) | (((*Color >> 8) & 0xFF) << 8) | (((*Color >> 16) & 0xFF) << 0);
            Red = (U8)((Converted >> 0) & 0xFF);
            Green = (U8)((Converted >> 8) & 0xFF);
            Blue = (U8)((Converted >> 16) & 0xFF);

            switch (Context->RasterOperation) {
                case ROP_SET:
                    Pixel[0] = Red;
                    Pixel[1] = Green;
                    Pixel[2] = Blue;
                    break;
                case ROP_XOR:
                    Pixel[0] ^= Red;
                    Pixel[1] ^= Green;
                    Pixel[2] ^= Blue;
                    break;
                case ROP_OR:
                    Pixel[0] |= Red;
                    Pixel[1] |= Green;
                    Pixel[2] |= Blue;
                    break;
                case ROP_AND:
                    Pixel[0] &= Red;
                    Pixel[1] &= Green;
                    Pixel[2] &= Blue;
                    break;
                default:
                    return FALSE;
            }

            *Color = Previous;
            return TRUE;
        }

        case 16:
            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 1);
            Pixel = Context->MemoryBase + Offset;
            Previous = *((U16*)Pixel);

            switch (Context->RasterOperation) {
                case ROP_SET:
                    *((U16*)Pixel) = (U16)(*Color);
                    break;
                case ROP_XOR:
                    *((U16*)Pixel) ^= (U16)(*Color);
                    break;
                case ROP_OR:
                    *((U16*)Pixel) |= (U16)(*Color);
                    break;
                case ROP_AND:
                    *((U16*)Pixel) &= (U16)(*Color);
                    break;
                default:
                    return FALSE;
            }

            *Color = Previous;
            return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Draw a line using current pen.
 * @param Context Active graphics context.
 * @param X1 Start X.
 * @param Y1 Start Y.
 * @param X2 End X.
 * @param Y2 End Y.
 */
static void GOPGfxDrawLine(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Dx = 0;
    I32 Sx = 0;
    I32 Dy = 0;
    I32 Sy = 0;
    I32 Error = 0;
    U32 Pattern = 0;
    U32 PatternBit = 0;
    COLOR Color = 0;

    if (Context == NULL || Context->Pen == NULL || Context->Pen->TypeID != KOID_PEN) {
        return;
    }

    Color = Context->Pen->Color;
    Pattern = Context->Pen->Pattern;
    if (Pattern == 0) {
        Pattern = MAX_U32;
    }

    Dx = (X2 >= X1) ? (X2 - X1) : (X1 - X2);
    Sx = X1 < X2 ? 1 : -1;
    Dy = -((Y2 >= Y1) ? (Y2 - Y1) : (Y1 - Y2));
    Sy = Y1 < Y2 ? 1 : -1;
    Error = Dx + Dy;

    for (;;) {
        if (((Pattern >> (PatternBit & 31)) & 1) != 0) {
            COLOR PixelColor = Color;
            (void)GOPGfxWritePixel(Context, X1, Y1, &PixelColor);
        }
        PatternBit++;

        if (X1 == X2 && Y1 == Y2) {
            break;
        }

        I32 DoubleError = Error << 1;
        if (DoubleError >= Dy) {
            Error += Dy;
            X1 += Sx;
        }
        if (DoubleError <= Dx) {
            Error += Dx;
            Y1 += Sy;
        }
    }
}

/************************************************************************/

/**
 * @brief Draw and fill a rectangle using current brush and pen.
 * @param Context Active graphics context.
 * @param X1 Left coordinate.
 * @param Y1 Top coordinate.
 * @param X2 Right coordinate.
 * @param Y2 Bottom coordinate.
 */
static void GOPGfxDrawRectangle(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 X = 0;
    I32 Y = 0;
    I32 Temp = 0;

    if (Context == NULL) {
        return;
    }

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

    if (Context->Brush != NULL && Context->Brush->TypeID == KOID_BRUSH) {
        for (Y = Y1; Y <= Y2; Y++) {
            for (X = X1; X <= X2; X++) {
                COLOR FillColor = Context->Brush->Color;
                (void)GOPGfxWritePixel(Context, X, Y, &FillColor);
            }
        }
    }

    if (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN) {
        GOPGfxDrawLine(Context, X1, Y1, X2, Y1);
        GOPGfxDrawLine(Context, X2, Y1, X2, Y2);
        GOPGfxDrawLine(Context, X2, Y2, X1, Y2);
        GOPGfxDrawLine(Context, X1, Y2, X1, Y1);
    }
}

/************************************************************************/

/**
 * @brief Load GOP backend from boot-provided framebuffer state.
 * @return DF_RETURN_SUCCESS on success.
 */
static UINT GOPGfxLoad(void) {
    UINT BytesPerPixel = 0;
    UINT FrameBufferSize = 0;

    if ((GOPGfxDriver.Flags & DRIVER_FLAG_READY) != 0) {
        return DF_RETURN_SUCCESS;
    }

    if (Console.FramebufferPhysical == 0 || Console.FramebufferWidth == 0 || Console.FramebufferHeight == 0 ||
        Console.FramebufferPitch == 0 || Console.FramebufferBitsPerPixel == 0 ||
        Console.FramebufferType != MULTIBOOT_FRAMEBUFFER_RGB) {
        DEBUG(TEXT("[GOPGfxLoad] No RGB boot framebuffer available"));
        return DF_RETURN_UNEXPECTED;
    }

    BytesPerPixel = Console.FramebufferBitsPerPixel / 8;
    if (BytesPerPixel == 0 || (BytesPerPixel != 2 && BytesPerPixel != 3 && BytesPerPixel != 4)) {
        WARNING(TEXT("[GOPGfxLoad] Unsupported framebuffer format bpp=%u"), Console.FramebufferBitsPerPixel);
        return DF_RETURN_UNEXPECTED;
    }

    FrameBufferSize = (UINT)(Console.FramebufferPitch * Console.FramebufferHeight);
    if (FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    GOPGfxState.FrameBufferLinear = MapFramebufferMemory(Console.FramebufferPhysical, FrameBufferSize);
    if (GOPGfxState.FrameBufferLinear == 0) {
        ERROR(TEXT("[GOPGfxLoad] MapFramebufferMemory failed for %p size=%u"),
            (LPVOID)(LINEAR)Console.FramebufferPhysical,
            FrameBufferSize);
        return DF_RETURN_UNEXPECTED;
    }

    GOPGfxState.FrameBufferPhysical = Console.FramebufferPhysical;
    GOPGfxState.FrameBufferSize = FrameBufferSize;

    GOPGfxState.Context = (GRAPHICSCONTEXT){
        .TypeID = KOID_GRAPHICSCONTEXT,
        .References = 1,
        .Mutex = EMPTY_MUTEX,
        .Driver = &GOPGfxDriver,
        .Width = (I32)Console.FramebufferWidth,
        .Height = (I32)Console.FramebufferHeight,
        .BitsPerPixel = Console.FramebufferBitsPerPixel,
        .BytesPerScanLine = Console.FramebufferPitch,
        .MemoryBase = (U8*)(LINEAR)GOPGfxState.FrameBufferLinear,
        .LoClip = {.X = 0, .Y = 0},
        .HiClip = {.X = (I32)Console.FramebufferWidth - 1, .Y = (I32)Console.FramebufferHeight - 1},
        .Origin = {.X = 0, .Y = 0},
        .RasterOperation = ROP_SET,
        .Brush = NULL,
        .Pen = NULL,
        .Font = NULL,
        .Bitmap = NULL
    };
    InitMutex(&(GOPGfxState.Context.Mutex));

    GOPGfxState.Capabilities = (GFX_CAPABILITIES){
        .Header = {.Size = sizeof(GFX_CAPABILITIES), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .HasHardwareModeset = FALSE,
        .HasPageFlip = FALSE,
        .HasVBlankInterrupt = FALSE,
        .HasCursorPlane = FALSE,
        .SupportsTiledSurface = FALSE,
        .MaxWidth = Console.FramebufferWidth,
        .MaxHeight = Console.FramebufferHeight,
        .PreferredFormat = (Console.FramebufferBitsPerPixel == 32) ? GFX_FORMAT_XRGB8888 :
                           (Console.FramebufferBitsPerPixel == 24) ? GFX_FORMAT_RGB888 : GFX_FORMAT_RGB565
    };

    DEBUG(TEXT("[GOPGfxLoad] Active mode %ux%u bpp=%u pitch=%u"),
        Console.FramebufferWidth,
        Console.FramebufferHeight,
        Console.FramebufferBitsPerPixel,
        Console.FramebufferPitch);

    GOPGfxDriver.Flags |= DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload GOP backend and release framebuffer mapping.
 * @return DF_RETURN_SUCCESS always.
 */
static UINT GOPGfxUnload(void) {
    if (GOPGfxState.FrameBufferLinear != 0 && GOPGfxState.FrameBufferSize != 0) {
        UnMapIOMemory(GOPGfxState.FrameBufferLinear, GOPGfxState.FrameBufferSize);
    }

    GOPGfxState = (GOP_GFX_STATE){0};
    GOPGfxDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Return current GOP mode information.
 * @param Info Output mode descriptor.
 * @return DF_RETURN_SUCCESS on success.
 */
static UINT GOPGfxGetModeInfo(LPGRAPHICSMODEINFO Info) {
    SAFE_USE(Info) {
        if (GOPGfxState.Context.Width <= 0 || GOPGfxState.Context.Height <= 0 || GOPGfxState.Context.BitsPerPixel == 0) {
            return DF_RETURN_UNEXPECTED;
        }
        Info->Width = (U32)GOPGfxState.Context.Width;
        Info->Height = (U32)GOPGfxState.Context.Height;
        Info->BitsPerPixel = GOPGfxState.Context.BitsPerPixel;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Keep active boot mode and report effective mode.
 * @param Info Requested/returned mode descriptor.
 * @return DF_RETURN_SUCCESS when context is active.
 */
static UINT GOPGfxSetMode(LPGRAPHICSMODEINFO Info) {
    if ((GOPGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    SAFE_USE(Info) {
        if ((Info->Width != 0 && Info->Width != (U32)GOPGfxState.Context.Width) ||
            (Info->Height != 0 && Info->Height != (U32)GOPGfxState.Context.Height)) {
            WARNING(TEXT("[GOPGfxSetMode] Requested %ux%u, keeping active %ux%u"),
                Info->Width,
                Info->Height,
                (U32)GOPGfxState.Context.Width,
                (U32)GOPGfxState.Context.Height);
        }

        Info->Width = (U32)GOPGfxState.Context.Width;
        Info->Height = (U32)GOPGfxState.Context.Height;
        Info->BitsPerPixel = GOPGfxState.Context.BitsPerPixel;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Return GOP capabilities.
 * @param Capabilities Output descriptor.
 * @return DF_RETURN_SUCCESS on success.
 */
static UINT GOPGfxGetCapabilities(LPGFX_CAPABILITIES Capabilities) {
    SAFE_USE(Capabilities) {
        *Capabilities = GOPGfxState.Capabilities;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Set a pixel in GOP framebuffer.
 * @param Info Pixel descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxSetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    COLOR PixelColor = 0;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    PixelColor = Info->Color;

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GOPGfxWritePixel(Context, Info->X, Info->Y, &PixelColor);
    UnlockMutex(&(Context->Mutex));

    if (!Result) {
        return 0;
    }

    Info->Color = PixelColor;
    return 1;
}

/************************************************************************/

/**
 * @brief Read a pixel from GOP framebuffer.
 * @param Info Pixel descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxGetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    U32 Offset = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT || Context->MemoryBase == NULL) {
        return 0;
    }

    if (Info->X < Context->LoClip.X || Info->X > Context->HiClip.X || Info->Y < Context->LoClip.Y || Info->Y > Context->HiClip.Y) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);

    if (Context->BitsPerPixel == 32) {
        Offset = (U32)(Info->Y * (I32)Context->BytesPerScanLine) + ((U32)Info->X << 2);
        Info->Color = *((U32*)(Context->MemoryBase + Offset));
    } else if (Context->BitsPerPixel == 24) {
        U8* Pixel = NULL;
        Offset = (U32)(Info->Y * (I32)Context->BytesPerScanLine) + ((U32)Info->X * 3);
        Pixel = Context->MemoryBase + Offset;
        Info->Color = (U32)Pixel[0] | ((U32)Pixel[1] << 8) | ((U32)Pixel[2] << 16);
    } else if (Context->BitsPerPixel == 16) {
        Offset = (U32)(Info->Y * (I32)Context->BytesPerScanLine) + ((U32)Info->X << 1);
        Info->Color = *((U16*)(Context->MemoryBase + Offset));
    } else {
        UnlockMutex(&(Context->Mutex));
        return 0;
    }

    UnlockMutex(&(Context->Mutex));
    return 1;
}

/************************************************************************/

/**
 * @brief Draw a line in GOP framebuffer.
 * @param Info Line descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxLine(LPLINEINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    GOPGfxDrawLine(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

/**
 * @brief Draw a rectangle in GOP framebuffer.
 * @param Info Rectangle descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxRectangle(LPRECTINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    GOPGfxDrawRectangle(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

/**
 * @brief Draw one text cell in GOP framebuffer.
 * @param Info Text cell descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxTextPutCell(LPGFX_TEXT_CELL_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);

    Result = GfxTextPutCell(Context, Info);

    UnlockMutex(&(Context->Mutex));

    return Result ? 1 : 0;
}

/************************************************************************/

/**
 * @brief Clear one text region in GOP framebuffer.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxTextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextClearRegion(Context, Info);
    UnlockMutex(&(Context->Mutex));

    return Result ? 1 : 0;
}

/************************************************************************/

/**
 * @brief Scroll one text region in GOP framebuffer.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxTextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextScrollRegion(Context, Info);
    UnlockMutex(&(Context->Mutex));

    return Result ? 1 : 0;
}

/************************************************************************/

/**
 * @brief Update cursor rendering in GOP framebuffer.
 * @param Info Cursor descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxTextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextSetCursor(Context, Info);
    UnlockMutex(&(Context->Mutex));

    return Result ? 1 : 0;
}

/************************************************************************/

/**
 * @brief Update cursor visibility in GOP backend.
 * @param Info Cursor visibility descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT GOPGfxTextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextSetCursorVisible(Context, Info);
    UnlockMutex(&(Context->Mutex));

    return Result ? 1 : 0;
}

/************************************************************************/

/**
 * @brief GOP graphics command dispatcher.
 * @param Function Driver function code.
 * @param Param Driver parameter.
 * @return Driver result code.
 */
static UINT GOPGfxCommands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return GOPGfxLoad();
        case DF_UNLOAD:
            return GOPGfxUnload();
        case DF_GET_VERSION:
            return MAKE_VERSION(GOP_GFX_VER_MAJOR, GOP_GFX_VER_MINOR);

        case DF_GFX_CREATECONTEXT:
            if ((GOPGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return 0;
            }
            return (UINT)(LPVOID)&GOPGfxState.Context;
        case DF_GFX_GETMODEINFO:
            return GOPGfxGetModeInfo((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_SETMODE:
            return GOPGfxSetMode((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_GETCAPABILITIES:
            return GOPGfxGetCapabilities((LPGFX_CAPABILITIES)Param);
        case DF_GFX_SETPIXEL:
            return GOPGfxSetPixel((LPPIXELINFO)Param);
        case DF_GFX_GETPIXEL:
            return GOPGfxGetPixel((LPPIXELINFO)Param);
        case DF_GFX_LINE:
            return GOPGfxLine((LPLINEINFO)Param);
        case DF_GFX_RECTANGLE:
            return GOPGfxRectangle((LPRECTINFO)Param);
        case DF_GFX_TEXT_PUTCELL:
            return GOPGfxTextPutCell((LPGFX_TEXT_CELL_INFO)Param);
        case DF_GFX_TEXT_CLEAR_REGION:
            return GOPGfxTextClearRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SCROLL_REGION:
            return GOPGfxTextScrollRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR:
            return GOPGfxTextSetCursor((LPGFX_TEXT_CURSOR_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
            return GOPGfxTextSetCursorVisible((LPGFX_TEXT_CURSOR_VISIBLE_INFO)Param);

        case DF_GFX_ENUMMODES:
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_ELLIPSE:
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

/************************************************************************/
