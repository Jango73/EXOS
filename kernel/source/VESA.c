
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "GFX.h"
#include "I386.h"
#include "Kernel.h"

#define MKLINPTR(a) (((a & 0xFFFF0000) >> 12) + (a & 0x0000FFFF))

#define CLIPVALUE(val, min, max) \
    {                            \
        if (val < min)           \
            val = min;           \
        else if (val > max)      \
            val = max;           \
    }

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 VESACommands(U32, U32);

DRIVER VESADriver = {ID_DRIVER,
                     1,
                     NULL,
                     NULL,
                     DRIVER_TYPE_GRAPHICS,
                     VER_MAJOR,
                     VER_MINOR,
                     "Jango73",
                     "Video Electronics Standard Association",
                     "VESA Compatible Graphics Card",
                     VESACommands};

/***************************************************************************/

typedef struct tag_VESACONTEXT VESACONTEXT, *LPVESACONTEXT;

/***************************************************************************/

static COLOR SetPixel8(LPVESACONTEXT, I32, I32, COLOR);
static COLOR SetPixel16(LPVESACONTEXT, I32, I32, COLOR);
static COLOR SetPixel24(LPVESACONTEXT, I32, I32, COLOR);
static COLOR GetPixel8(LPVESACONTEXT, I32, I32);
static COLOR GetPixel16(LPVESACONTEXT, I32, I32);
static COLOR GetPixel24(LPVESACONTEXT, I32, I32);
static U32 Line8(LPVESACONTEXT, I32, I32, I32, I32);
static U32 Line16(LPVESACONTEXT, I32, I32, I32, I32);
static U32 Line24(LPVESACONTEXT, I32, I32, I32, I32);
static U32 Rect8(LPVESACONTEXT, I32, I32, I32, I32);
static U32 Rect16(LPVESACONTEXT, I32, I32, I32, I32);
static U32 Rect24(LPVESACONTEXT, I32, I32, I32, I32);

/***************************************************************************/

typedef struct tag_VESAINFOBLOCK {
    U8 Signature[4];  // 4 signature bytes
    U16 Version;      // VESA version number
    U32 OEMString;    // Pointer to OEM string
    U8 Caps[4];       // Capabilities of the video environment
    U32 ModePointer;  // Pointer to supported Super VGA modes
    U16 Memory;       // Number of 64kb memory blocks on board
} VESAINFOBLOCK, *LPVESAINFOBLOCK;

/***************************************************************************/

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
    U8 DirectColorModeInfo;
} MODEINFOBLOCK, *LPMODEINFOBLOCK;

/***************************************************************************/

typedef struct tag_VIDEOMODESPECS {
    U32 Mode;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    COLOR (*SetPixel)(LPVESACONTEXT, I32, I32, COLOR);
    COLOR (*GetPixel)(LPVESACONTEXT, I32, I32);
    U32 (*Line)(LPVESACONTEXT, I32, I32, I32, I32);
    U32 (*Rect)(LPVESACONTEXT, I32, I32, I32, I32);
} VIDEOMODESPECS, *LPVIDEOMODESPECS;

/***************************************************************************/

struct tag_VESACONTEXT {
    GRAPHICSCONTEXT Header;
    VESAINFOBLOCK VESAInfo;
    MODEINFOBLOCK ModeInfo;
    VIDEOMODESPECS ModeSpecs;
    U32 Granularity;
    U32 GranularShift;
    U32 GranularModulo;
    U32 NumBanks;
    U32 CurrentBank;
    U32 PixelSize;
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

#define SWITCHBANK(c, b)                 \
    if (b != c->CurrentBank) {           \
        Regs.X.AX = 0x4F05;              \
        Regs.X.DX = b;                   \
        Regs.X.BX = 0;                   \
        RealModeCall(VIDEO_CALL, &Regs); \
        c->CurrentBank = b;              \
    }

VESACONTEXT VESAContext = {0};

/***************************************************************************/

static U32 VESAInitialize() {
    X86REGS Regs;

    //-------------------------------------
    // Initialize the context

    MemorySet(&VESAContext, 0, sizeof(VESACONTEXT));

    InitMutex(&(VESAContext.Header.Mutex));

    VESAContext.Header.ID = ID_GRAPHICSCONTEXT;
    VESAContext.Header.References = 1;
    VESAContext.Header.Driver = &VESADriver;
    VESAContext.Header.LoClip.X = 0;
    VESAContext.Header.LoClip.Y = 0;
    VESAContext.Header.HiClip.X = 100;
    VESAContext.Header.HiClip.Y = 100;
    VESAContext.Header.RasterOperation = ROP_SET;

    VESAContext.ModeSpecs.SetPixel = SetPixel8;
    VESAContext.ModeSpecs.GetPixel = GetPixel8;
    VESAContext.ModeSpecs.Line = Line8;
    VESAContext.ModeSpecs.Rect = Rect8;

    //-------------------------------------
    // Get VESA general information

    Regs.X.AX = 0x4F00;
    Regs.X.ES = (StubAddress + N_4KB) >> MUL_16;
    Regs.X.DI = 0;

    RealModeCall(VIDEO_CALL, &Regs);

    MemoryCopy(&(VESAContext.VESAInfo), (LPVOID)(StubAddress + N_4KB),
               sizeof(VESAINFOBLOCK));

    if (VESAContext.VESAInfo.Signature[0] != 'V') return DF_ERROR_GENERIC;
    if (VESAContext.VESAInfo.Signature[1] != 'E') return DF_ERROR_GENERIC;
    if (VESAContext.VESAInfo.Signature[2] != 'S') return DF_ERROR_GENERIC;
    if (VESAContext.VESAInfo.Signature[3] != 'A') return DF_ERROR_GENERIC;

    KernelPrint("\n");
    KernelPrint("VESA driver version %d.%d\n", VER_MAJOR, VER_MINOR);
    KernelPrint("Manufacturer : %s\n",
                MKLINPTR(VESAContext.VESAInfo.OEMString));
    KernelPrint("Version      : %d\n", VESAContext.VESAInfo.Version);
    KernelPrint("Total memory : %d KB\n",
                (VESAContext.VESAInfo.Memory << MUL_64KB) >> MUL_1KB);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 VESAUninitialize() {
    X86REGS Regs;

    //-------------------------------------
    // Set text mode

    Regs.X.AX = 0x4F02;
    Regs.X.BX = 0x03;
    RealModeCall(VIDEO_CALL, &Regs);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 SetVideoMode(LPGRAPHICSMODEINFO Info) {
    X86REGS Regs;
    U16* ModePtr = NULL;
    U32 Found = 0;
    U32 Index = 0;
    U32 Mode = 0;

    for (Index = 0;; Index++) {
        if (VESAModeSpecs[Index].Mode == 0) return DF_ERROR_GENERIC;

        if (VESAModeSpecs[Index].Width == Info->Width &&
            VESAModeSpecs[Index].Height == Info->Height &&
            VESAModeSpecs[Index].BitsPerPixel == Info->BitsPerPixel) {
            ModePtr = (U16*)MKLINPTR(VESAContext.VESAInfo.ModePointer);

            if (ModePtr == NULL) return DF_ERROR_GENERIC;

            for (Mode = 0;; Mode++) {
                if (ModePtr[Mode] == 0xFFFF) break;

                if (ModePtr[Mode] == VESAModeSpecs[Index].Mode) {
                    MemoryCopy(&(VESAContext.ModeSpecs), VESAModeSpecs + Index,
                               sizeof(VIDEOMODESPECS));
                    Found = 1;
                    break;
                }
            }
        }

        if (Found == 1) break;
    }

    if (Found == 0) return DF_ERROR_GENERIC;

    //-------------------------------------
    // Get info about the mode

    Regs.X.AX = 0x4F01;
    Regs.X.CX = VESAContext.ModeSpecs.Mode;
    Regs.X.ES = (StubAddress + N_4KB) >> MUL_16;
    Regs.X.DI = 0;
    RealModeCall(VIDEO_CALL, &Regs);

    if (Regs.H.AL != 0x4F) return DF_ERROR_GENERIC;

    MemoryCopy(&(VESAContext.ModeInfo), (LPVOID)(StubAddress + N_4KB),
               sizeof(MODEINFOBLOCK));

    VESAContext.Header.MemoryBase =
        (U8*)(VESAContext.ModeInfo.WindowAStartSegment << MUL_16);
    VESAContext.Header.BytesPerScanLine = VESAContext.ModeInfo.BytesPerScanLine;
    VESAContext.Granularity = VESAContext.ModeInfo.WindowGranularity;

    if (VESAContext.Granularity < 1) VESAContext.Granularity = 1;
    if (VESAContext.Granularity > 64) VESAContext.Granularity = 64;
    VESAContext.Granularity *= 1024;

    //-------------------------------------
    // Set the mode

    Regs.X.AX = 0x4F02;
    Regs.X.BX = VESAContext.ModeSpecs.Mode;
    RealModeCall(VIDEO_CALL, &Regs);

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

    switch (VESAContext.Granularity) {
        case 256:
            VESAContext.GranularShift = 8;
            break;
        case 512:
            VESAContext.GranularShift = 9;
            break;
        case 1024:
            VESAContext.GranularShift = 10;
            break;
        case 2048:
            VESAContext.GranularShift = 11;
            break;
        case 4096:
            VESAContext.GranularShift = 12;
            break;
        case 8192:
            VESAContext.GranularShift = 13;
            break;
        case 16384:
            VESAContext.GranularShift = 14;
            break;
        case 32768:
            VESAContext.GranularShift = 15;
            break;
        case 65536:
            VESAContext.GranularShift = 16;
            break;
        case 131072:
            VESAContext.GranularShift = 17;
            break;
        case 262144:
            VESAContext.GranularShift = 18;
            break;
        case 524288:
            VESAContext.GranularShift = 19;
            break;
        case 1048576:
            VESAContext.GranularShift = 20;
            break;
        case 2097152:
            VESAContext.GranularShift = 21;
            break;
        case 4194304:
            VESAContext.GranularShift = 22;
            break;
        default:
            VESAContext.GranularShift = 16;
            break;
    }

    VESAContext.GranularModulo = VESAContext.Granularity - 1;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static void SetVESABank(LPVESACONTEXT Context, U32 Bank) {
    X86REGS Regs;

    if (Bank != Context->CurrentBank) {
        Regs.X.AX = 0x4F05;
        Regs.X.DX = Bank;
        Regs.X.BX = 0;
        RealModeCall(VIDEO_CALL, &Regs);
        Context->CurrentBank = Bank;
    }
}

/***************************************************************************/

static U32 SetClip(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
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

/***************************************************************************/

static COLOR SetPixel8(LPVESACONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Bank;
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return;

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Bank = Offset >> Context->GranularShift;

    SetVESABank(Context, Bank);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            *(U8*)(Context->Header.MemoryBase +
                   (Offset & Context->GranularModulo)) = Color;
        }
            return;

        case ROP_XOR: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *Plane ^= Color;
        }
            return;

        case ROP_OR: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *Plane |= Color;
        }
            return;

        case ROP_AND: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *Plane &= Color;
        }
            return;
    }

    return OldColor;
}

/***************************************************************************/

static COLOR SetPixel16(LPVESACONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Bank;
    U32 Offset;
    U8* Plane;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Bank = Offset >> Context->GranularShift;

    SetVESABank(Context, Bank);

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *((U16*)Plane) = Color;
        } break;

        case ROP_XOR: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *((U16*)Plane) ^= Color;
        } break;

        case ROP_OR: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *((U16*)Plane) |= Color;
        } break;

        case ROP_AND: {
            Plane =
                Context->Header.MemoryBase + (Offset & Context->GranularModulo);
            *((U16*)Plane) &= Color;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

static COLOR SetPixel24(LPVESACONTEXT Context, I32 X, I32 Y, COLOR Color) {
    U32 Bank;
    U32 Offset;
    U8* Plane;
    U32 TOfs1, TOfs2, TOfs3;
    U32 Bank1, Bank2, Bank3;
    U32 R, G, B, A;
    U32 OldColor;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return;

    Offset = (Y * Context->Header.BytesPerScanLine) + (X * 3);

    TOfs1 = Offset + 0;
    Bank1 = TOfs1 >> Context->GranularShift;
    TOfs2 = Offset + 1;
    Bank2 = TOfs2 >> Context->GranularShift;
    TOfs3 = Offset + 2;
    Bank3 = TOfs3 >> Context->GranularShift;

    OldColor = 0;
    OldColor |= (((Color >> 0) & 0xFF) << 16);
    OldColor |= (((Color >> 8) & 0xFF) << 8);
    OldColor |= (((Color >> 16) & 0xFF) << 0);

    R = ((OldColor >> 0) & 0xFF);
    G = ((OldColor >> 8) & 0xFF);
    B = ((OldColor >> 16) & 0xFF);
    A = ((OldColor >> 24) & 0xFF);

    OldColor = 0;

    switch (Context->Header.RasterOperation) {
        case ROP_SET: {
            // Red component
            SetVESABank(Context, Bank1);
            Plane =
                Context->Header.MemoryBase + (TOfs1 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 0;
            *Plane = R;

            // Green component
            SetVESABank(Context, Bank2);
            Plane =
                Context->Header.MemoryBase + (TOfs2 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 8;
            *Plane = G;

            // Blue component
            SetVESABank(Context, Bank3);
            Plane =
                Context->Header.MemoryBase + (TOfs3 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 16;
            *Plane = B;
        } break;

        case ROP_XOR: {
            // Red component

            SetVESABank(Context, Bank1);
            Plane =
                Context->Header.MemoryBase + (TOfs1 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 0;
            *Plane ^= R;

            // Green component

            SetVESABank(Context, Bank2);
            Plane =
                Context->Header.MemoryBase + (TOfs2 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 8;
            *Plane ^= G;

            // Blue component

            SetVESABank(Context, Bank3);
            Plane =
                Context->Header.MemoryBase + (TOfs3 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 16;
            *Plane ^= B;
        } break;

        case ROP_OR: {
            // Red component
            SetVESABank(Context, Bank1);
            Plane =
                Context->Header.MemoryBase + (TOfs1 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 0;
            *Plane |= R;

            // Green component
            SetVESABank(Context, Bank2);
            Plane =
                Context->Header.MemoryBase + (TOfs2 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 8;
            *Plane |= G;

            // Blue component
            SetVESABank(Context, Bank3);
            Plane =
                Context->Header.MemoryBase + (TOfs3 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 16;
            *Plane |= B;
        } break;

        case ROP_AND: {
            // Red component
            SetVESABank(Context, Bank1);
            Plane =
                Context->Header.MemoryBase + (TOfs1 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 0;
            *Plane &= R;

            // Green component
            SetVESABank(Context, Bank2);
            Plane =
                Context->Header.MemoryBase + (TOfs2 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 8;
            *Plane &= G;

            // Blue component
            SetVESABank(Context, Bank3);
            Plane =
                Context->Header.MemoryBase + (TOfs3 & Context->GranularModulo);
            OldColor |= (U32)(*Plane) << 16;
            *Plane &= B;
        } break;
    }

    return OldColor;
}

/***************************************************************************/

static COLOR GetPixel8(LPVESACONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;
    U32 Bank;
    U8* Plane;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + X;
    Bank = Offset >> Context->GranularShift;

    SetVESABank(Context, Bank);

    Color = *(
        (U8*)(Context->Header.MemoryBase + (Offset & Context->GranularModulo)));

    return Color;
}

/***************************************************************************/

static COLOR GetPixel16(LPVESACONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Offset;
    U32 Bank;
    U8* Plane;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    Offset = (Y * Context->Header.BytesPerScanLine) + (X << MUL_2);
    Bank = Offset >> Context->GranularShift;

    SetVESABank(Context, Bank);

    Color = *((U16*)(Context->Header.MemoryBase +
                     (Offset & Context->GranularModulo)));

    return Color;
}

/***************************************************************************/

static COLOR GetPixel24(LPVESACONTEXT Context, I32 X, I32 Y) {
    U32 Color;
    U32 Bank;
    U8* Plane;
    U32 TOfs1, TOfs2, TOfs3;
    U32 Bank1, Bank2, Bank3;

    if (X < Context->Header.LoClip.X || X > Context->Header.HiClip.X ||
        Y < Context->Header.LoClip.Y || Y > Context->Header.HiClip.Y)
        return 0;

    /*
      if ( (x < 0) || (x > Context.ScreenWidth - 1) ||
       (y < 0) || (y > Context.ScreenHeight - 1) ) return 0;
    */

    TOfs1 = (Y * Context->Header.BytesPerScanLine) + ((X * 3) + 0);
    TOfs2 = (Y * Context->Header.BytesPerScanLine) + ((X * 3) + 1);
    TOfs3 = (Y * Context->Header.BytesPerScanLine) + ((X * 3) + 2);

    Bank1 = TOfs1 >> Context->GranularShift;
    Bank2 = TOfs2 >> Context->GranularShift;
    Bank3 = TOfs3 >> Context->GranularShift;

    Color = 0;

    // Red component
    SetVESABank(Context, Bank1);
    Plane = Context->Header.MemoryBase + (TOfs1 & Context->GranularModulo);
    Color |= (U32)(*Plane);

    // Green component
    SetVESABank(Context, Bank2);
    Plane = Context->Header.MemoryBase + (TOfs2 & Context->GranularModulo);
    Color |= (U32)(*Plane) << 8;

    // Blue component
    SetVESABank(Context, Bank3);
    Plane = Context->Header.MemoryBase + (TOfs3 & Context->GranularModulo);
    Color |= (U32)(*Plane) << 16;

    return Color;
}

/***************************************************************************/

static U32 Line8(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
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

static U32 Line16(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 d, dx, dy, ai, bi, xi, yi;
    U32 LineBit = 0;
    U32 Pattern;
    COLOR Color;

    if (Context->Header.Pen == NULL) return MAX_U32;
    if (Context->Header.Pen->ID != ID_PEN) return MAX_U32;

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

static U32 Line24(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 d, dx, dy, ai, bi, xi, yi;
    U32 LineBit = 0;
    U32 Pattern;
    COLOR Color;

    if (Context->Header.Pen == NULL) return MAX_U32;
    if (Context->Header.Pen->ID != ID_PEN) return MAX_U32;

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

static U32 Rect8(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
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
}

/***************************************************************************/

static U32 Rect16(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    U32 X, Y;
    U32 Temp;
    U32 Pattern;
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

    if (Context->Header.Brush != NULL &&
        Context->Header.Brush->ID == ID_BRUSH) {
        Color = Context->Header.Brush->Color;
        Pattern = Context->Header.Brush->Pattern;

        for (Y = Y1; Y <= Y2; Y++) {
            for (X = X1; X <= X2; X++) {
                Context->ModeSpecs.SetPixel(Context, X, Y, Color);
            }
        }
    }

    if (Context->Header.Pen != NULL && Context->Header.Pen->ID == ID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

static U32 Rect24(LPVESACONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    X86REGS Regs;
    U8* Pln1;
    U8* Pln2;
    U8* Pln3;
    U32 Bank;
    U32 Offset;
    U32 Ofs1, Ofs2, Ofs3;
    U32 Bank1, Bank2, Bank3;
    U32 R, G, B, A;
    I32 X, Y;
    U32 Temp;
    U32 Pattern;
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

    if (Context->Header.Brush != NULL &&
        Context->Header.Brush->ID == ID_BRUSH) {
        Color = Context->Header.Brush->Color;
        Pattern = Context->Header.Brush->Pattern;

        Color = 0;
        Color |= (((Context->Header.Brush->Color >> 0) & 0xFF) << 16);
        Color |= (((Context->Header.Brush->Color >> 8) & 0xFF) << 8);
        Color |= (((Context->Header.Brush->Color >> 16) & 0xFF) << 0);

        if (X1 < Context->Header.LoClip.X) X1 = Context->Header.LoClip.X;
        if (X1 > Context->Header.HiClip.X) X1 = Context->Header.HiClip.X;
        if (X2 < Context->Header.LoClip.X) X2 = Context->Header.LoClip.X;
        if (X2 > Context->Header.HiClip.X) X2 = Context->Header.HiClip.X;
        if (Y1 < Context->Header.LoClip.Y) Y1 = Context->Header.LoClip.Y;
        if (Y1 > Context->Header.HiClip.Y) Y1 = Context->Header.HiClip.Y;
        if (Y2 < Context->Header.LoClip.Y) Y2 = Context->Header.LoClip.Y;
        if (Y2 > Context->Header.HiClip.Y) Y2 = Context->Header.HiClip.Y;

        Offset = (Y1 * Context->Header.BytesPerScanLine) + (X1 * 3);

        Ofs1 = Offset + 0;
        Bank1 = Ofs1 >> Context->GranularShift;
        Ofs2 = Offset + 1;
        Bank2 = Ofs2 >> Context->GranularShift;
        Ofs3 = Offset + 2;
        Bank3 = Ofs3 >> Context->GranularShift;

        Pln1 = Context->Header.MemoryBase + (Ofs1 & Context->GranularModulo);
        Pln2 = Context->Header.MemoryBase + (Ofs2 & Context->GranularModulo);
        Pln3 = Context->Header.MemoryBase + (Ofs3 & Context->GranularModulo);

        R = (Color >> 0) & 0xFF;
        G = (Color >> 8) & 0xFF;
        B = (Color >> 16) & 0xFF;
        A = (Color >> 24) & 0xFF;

        for (Y = Y1; Y <= Y2; Y++) {
            for (X = X1; X <= X2; X++) {
                switch (Context->Header.RasterOperation) {
                    case ROP_SET: {
                        SWITCHBANK(Context, Bank1);
                        *(Pln1) = R;
                        SWITCHBANK(Context, Bank2);
                        *(Pln2) = G;
                        SWITCHBANK(Context, Bank3);
                        *(Pln3) = B;
                    } break;

                    case ROP_XOR: {
                        SWITCHBANK(Context, Bank1);
                        (*Pln1) ^= R;
                        SWITCHBANK(Context, Bank2);
                        (*Pln2) ^= G;
                        SWITCHBANK(Context, Bank3);
                        (*Pln3) ^= B;
                    } break;

                    case ROP_OR: {
                        SWITCHBANK(Context, Bank1);
                        (*Pln1) |= R;
                        SWITCHBANK(Context, Bank2);
                        (*Pln2) |= G;
                        SWITCHBANK(Context, Bank3);
                        (*Pln3) |= B;
                    } break;

                    case ROP_AND: {
                        SWITCHBANK(Context, Bank1);
                        (*Pln1) &= R;
                        SWITCHBANK(Context, Bank2);
                        (*Pln2) &= G;
                        SWITCHBANK(Context, Bank3);
                        (*Pln3) &= B;
                    } break;
                }

                Pln1 += 3;
                Pln2 += 3;
                Pln3 += 3;
            }

            Offset += Context->Header.BytesPerScanLine;

            Ofs1 = Offset + 0;
            Bank1 = Ofs1 >> Context->GranularShift;
            Ofs2 = Offset + 1;
            Bank2 = Ofs2 >> Context->GranularShift;
            Ofs3 = Offset + 2;
            Bank3 = Ofs3 >> Context->GranularShift;

            Pln1 =
                Context->Header.MemoryBase + (Ofs1 & Context->GranularModulo);
            Pln2 =
                Context->Header.MemoryBase + (Ofs2 & Context->GranularModulo);
            Pln3 =
                Context->Header.MemoryBase + (Ofs3 & Context->GranularModulo);
        }
    }

    // Draw borders

    if (Context->Header.Pen != NULL && Context->Header.Pen->ID == ID_PEN) {
        Context->ModeSpecs.Line(Context, X1, Y1, X2, Y1);
        Context->ModeSpecs.Line(Context, X2, Y1, X2, Y2);
        Context->ModeSpecs.Line(Context, X2, Y2, X1, Y2);
        Context->ModeSpecs.Line(Context, X1, Y2, X1, Y1);
    }

    return 0;
}

/***************************************************************************/

static LPBRUSH VESA_CreateBrush(LPBRUSHINFO Info) {
    LPBRUSH Brush;

    if (Info == NULL) return NULL;

    Brush = (LPBRUSH)HeapAlloc(sizeof(BRUSH));
    if (Brush == NULL) return NULL;

    MemorySet(Brush, 0, sizeof(BRUSH));

    Brush->ID = ID_BRUSH;
    Brush->References = 1;
    Brush->Color = Info->Color;
    Brush->Pattern = Info->Pattern;

    return Brush;
}

/***************************************************************************/

static LPPEN VESA_CreatePen(LPPENINFO Info) {
    LPPEN Pen;

    if (Info == NULL) return NULL;

    Pen = (LPPEN)HeapAlloc(sizeof(PEN));
    if (Pen == NULL) return NULL;

    MemorySet(Pen, 0, sizeof(PEN));

    Pen->ID = ID_BRUSH;
    Pen->References = 1;
    Pen->Color = Info->Color;
    Pen->Pattern = Info->Pattern;

    return Pen;
}

/***************************************************************************/

static U32 VESA_SetPixel(LPPIXELINFO Info) {
    LPVESACONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESACONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.ID != ID_GRAPHICSCONTEXT) return 0;

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Info->Color =
        Context->ModeSpecs.SetPixel(Context, Info->X, Info->Y, Info->Color);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

static U32 VESA_GetPixel(LPPIXELINFO Info) {
    LPVESACONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESACONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.ID != ID_GRAPHICSCONTEXT) return 0;

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Info->Color = Context->ModeSpecs.GetPixel(Context, Info->X, Info->Y);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

static U32 VESA_Line(LPLINEINFO Info) {
    LPVESACONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESACONTEXT)Info->GC;

    // if (Context == NULL) return 0;
    if (Context == NULL) Context = &VESAContext;
    if (Context->Header.ID != ID_GRAPHICSCONTEXT) return 0;

    // LockMutex(&(Context->Header.Mutex), INFINITY);

    Context->ModeSpecs.Line(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);

    // UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

static U32 VESA_Rectangle(LPRECTINFO Info) {
    LPVESACONTEXT Context;

    if (Info == NULL) return 0;

    Context = (LPVESACONTEXT)Info->GC;

    if (Context == NULL) return 0;
    if (Context->Header.ID != ID_GRAPHICSCONTEXT) return 0;

    LockMutex(&(Context->Header.Mutex), INFINITY);

    Context->ModeSpecs.Rect(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);

    UnlockMutex(&(Context->Header.Mutex));

    return 1;
}

/***************************************************************************/

U32 VESACommands(U32 Function, U32 Param) {
    switch (Function) {
        case DF_LOAD:
            return (U32)VESAInitialize();
        case DF_UNLOAD:
            return (U32)VESAUninitialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_GFX_SETMODE:
            return SetVideoMode((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_CREATEBRUSH:
            return (U32)VESA_CreateBrush((LPBRUSHINFO)Param);
        case DF_GFX_CREATEPEN:
            return (U32)VESA_CreatePen((LPPENINFO)Param);
        case DF_GFX_SETPIXEL:
            return VESA_SetPixel((LPPIXELINFO)Param);
        case DF_GFX_GETPIXEL:
            return VESA_GetPixel((LPPIXELINFO)Param);
        case DF_GFX_LINE:
            return VESA_Line((LPLINEINFO)Param);
        case DF_GFX_RECTANGLE:
            return VESA_Rectangle((LPRECTINFO)Param);
    }

    return DF_ERROR_NOTIMPL;
}

/***************************************************************************/
