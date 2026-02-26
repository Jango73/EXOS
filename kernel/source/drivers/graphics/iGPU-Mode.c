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


    Intel graphics (mode takeover and native setmode)

\************************************************************************/

#include "iGPU-Internal.h"

#include "Clock.h"
#include "Log.h"
#include "Memory.h"

/************************************************************************/

static const U32 IntelPipeConfRegisters[] = {INTEL_REG_PIPE_A_CONF, INTEL_REG_PIPE_B_CONF, INTEL_REG_PIPE_C_CONF};
static const U32 IntelPipeSourceRegisters[] = {INTEL_REG_PIPE_A_SRC, INTEL_REG_PIPE_B_SRC, INTEL_REG_PIPE_C_SRC};
static const U32 IntelPipeHTotalRegisters[] = {INTEL_REG_PIPE_A_HTOTAL, INTEL_REG_PIPE_B_HTOTAL, INTEL_REG_PIPE_C_HTOTAL};
static const U32 IntelPipeHBlankRegisters[] = {INTEL_REG_PIPE_A_HBLANK, INTEL_REG_PIPE_B_HBLANK, INTEL_REG_PIPE_C_HBLANK};
static const U32 IntelPipeHSyncRegisters[] = {INTEL_REG_PIPE_A_HSYNC, INTEL_REG_PIPE_B_HSYNC, INTEL_REG_PIPE_C_HSYNC};
static const U32 IntelPipeVTotalRegisters[] = {INTEL_REG_PIPE_A_VTOTAL, INTEL_REG_PIPE_B_VTOTAL, INTEL_REG_PIPE_C_VTOTAL};
static const U32 IntelPipeVBlankRegisters[] = {INTEL_REG_PIPE_A_VBLANK, INTEL_REG_PIPE_B_VBLANK, INTEL_REG_PIPE_C_VBLANK};
static const U32 IntelPipeVSyncRegisters[] = {INTEL_REG_PIPE_A_VSYNC, INTEL_REG_PIPE_B_VSYNC, INTEL_REG_PIPE_C_VSYNC};
static const U32 IntelPlaneControlRegisters[] = {INTEL_REG_PLANE_A_CTL, INTEL_REG_PLANE_B_CTL, INTEL_REG_PLANE_C_CTL};
static const U32 IntelPlaneStrideRegisters[] = {INTEL_REG_PLANE_A_STRIDE, INTEL_REG_PLANE_B_STRIDE, INTEL_REG_PLANE_C_STRIDE};
static const U32 IntelPlaneSurfaceRegisters[] = {INTEL_REG_PLANE_A_SURF, INTEL_REG_PLANE_B_SURF, INTEL_REG_PLANE_C_SURF};

/************************************************************************/

static U32 IntelGfxResolveBitsPerPixel(U32 PlaneControlValue) {
    U32 Format = PlaneControlValue & INTEL_PLANE_CTL_FORMAT_MASK;

    switch (Format) {
        case (0x02 << 24):
            return 16;
        case (0x04 << 24):
            return 32;
        case (0x06 << 24):
            return 32;
        default:
            return 32;
    }
}

/************************************************************************/

static BOOL IntelGfxReadActiveScanoutState(void) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0]); Index++) {
        U32 PipeConf = 0;
        U32 PipeSrc = 0;
        U32 PlaneControl = 0;
        U32 PlaneStride = 0;
        U32 PlaneSurface = 0;
        U32 Width = 0;
        U32 Height = 0;
        U32 BitsPerPixel = 0;
        U32 Stride = 0;

        if (!IntelGfxReadMmio32(IntelPipeConfRegisters[Index], &PipeConf)) continue;
        if ((PipeConf & INTEL_PIPE_CONF_ENABLE) == 0) continue;

        if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[Index], &PlaneControl)) continue;
        if ((PlaneControl & INTEL_PLANE_CTL_ENABLE) == 0) continue;

        if (!IntelGfxReadMmio32(IntelPipeSourceRegisters[Index], &PipeSrc)) continue;
        if (!IntelGfxReadMmio32(IntelPlaneStrideRegisters[Index], &PlaneStride)) continue;
        if (!IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[Index], &PlaneSurface)) continue;

        Width = (PipeSrc & 0x1FFF) + 1;
        Height = ((PipeSrc >> 16) & 0x1FFF) + 1;
        BitsPerPixel = IntelGfxResolveBitsPerPixel(PlaneControl);
        Stride = PlaneStride & 0x0001FFFC;
        if (Stride == 0) {
            Stride = Width * (BitsPerPixel >> 3);
        }

        IntelGfxState.ActivePipeIndex = Index;
        IntelGfxState.ActiveWidth = Width;
        IntelGfxState.ActiveHeight = Height;
        IntelGfxState.ActiveBitsPerPixel = BitsPerPixel;
        IntelGfxState.ActiveStride = Stride;
        IntelGfxState.ActiveSurfaceOffset = PlaneSurface & INTEL_SURFACE_ALIGN_MASK;

        DEBUG(TEXT("[IntelGfxReadActiveScanoutState] Pipe=%u Width=%u Height=%u Bpp=%u Stride=%u Surface=%x"),
            Index, Width, Height, BitsPerPixel, Stride, IntelGfxState.ActiveSurfaceOffset);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL IntelGfxReadModeProgram(U32 PipeIndex, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    if (ProgramOut == NULL || PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    ProgramOut->PipeIndex = PipeIndex;
    ProgramOut->Width = IntelGfxState.ActiveWidth;
    ProgramOut->Height = IntelGfxState.ActiveHeight;
    ProgramOut->BitsPerPixel = IntelGfxState.ActiveBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;

    return IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &ProgramOut->PipeConf) &&
           IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &ProgramOut->PipeSource) &&
           IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &ProgramOut->PipeHTotal) &&
           IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &ProgramOut->PipeHBlank) &&
           IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &ProgramOut->PipeHSync) &&
           IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &ProgramOut->PipeVTotal) &&
           IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &ProgramOut->PipeVBlank) &&
           IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &ProgramOut->PipeVSync) &&
           IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &ProgramOut->PlaneControl) &&
           IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &ProgramOut->PlaneStride) &&
           IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &ProgramOut->PlaneSurface);
}

/************************************************************************/

static BOOL IntelGfxWaitPipeState(U32 PipeIndex, BOOL EnabledExpected) {
    UINT StartTime = GetSystemTime();
    UINT Loop = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    for (Loop = 0; HasOperationTimedOut(StartTime, Loop, INTEL_MODESET_LOOP_LIMIT, INTEL_MODESET_TIMEOUT_MILLISECONDS) == FALSE; Loop++) {
        U32 PipeConf = 0;
        BOOL Enabled = FALSE;

        if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
            return FALSE;
        }

        Enabled = (PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
        if (Enabled == EnabledExpected) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static UINT IntelGfxDisablePipe(U32 PipeIndex) {
    U32 PlaneControl = 0;
    U32 PipeConf = 0;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl)) {
        return DF_RETURN_UNEXPECTED;
    }

    PlaneControl &= ~INTEL_PLANE_CTL_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPlaneControlRegisters[PipeIndex], PlaneControl)) {
        return DF_RETURN_UNEXPECTED;
    }
    (void)IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl);

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeConf &= ~INTEL_PIPE_CONF_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPipeConfRegisters[PipeIndex], PipeConf)) {
        return DF_RETURN_UNEXPECTED;
    }
    (void)IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf);

    if (!IntelGfxWaitPipeState(PipeIndex, FALSE)) {
        ERROR(TEXT("[IntelGfxDisablePipe] Pipe=%u disable timeout"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxEnablePipe(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 PipeConf = 0;
    U32 PlaneControl = 0;
    U32 PipeIndex = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeIndex = Program->PipeIndex;
    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWriteMmio32(IntelPipeHTotalRegisters[PipeIndex], Program->PipeHTotal) ||
        !IntelGfxWriteMmio32(IntelPipeHBlankRegisters[PipeIndex], Program->PipeHBlank) ||
        !IntelGfxWriteMmio32(IntelPipeHSyncRegisters[PipeIndex], Program->PipeHSync) ||
        !IntelGfxWriteMmio32(IntelPipeVTotalRegisters[PipeIndex], Program->PipeVTotal) ||
        !IntelGfxWriteMmio32(IntelPipeVBlankRegisters[PipeIndex], Program->PipeVBlank) ||
        !IntelGfxWriteMmio32(IntelPipeVSyncRegisters[PipeIndex], Program->PipeVSync) ||
        !IntelGfxWriteMmio32(IntelPipeSourceRegisters[PipeIndex], Program->PipeSource) ||
        !IntelGfxWriteMmio32(IntelPlaneStrideRegisters[PipeIndex], Program->PlaneStride) ||
        !IntelGfxWriteMmio32(IntelPlaneSurfaceRegisters[PipeIndex], Program->PlaneSurface)) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeConf = Program->PipeConf | INTEL_PIPE_CONF_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPipeConfRegisters[PipeIndex], PipeConf)) {
        return DF_RETURN_UNEXPECTED;
    }

    PlaneControl = Program->PlaneControl;
    PlaneControl &= ~INTEL_PLANE_CTL_FORMAT_MASK;
    PlaneControl |= INTEL_PLANE_CTL_FORMAT_XRGB8888;
    PlaneControl |= INTEL_PLANE_CTL_ENABLE;
    if (!IntelGfxWriteMmio32(IntelPlaneControlRegisters[PipeIndex], PlaneControl)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWaitPipeState(PipeIndex, TRUE)) {
        ERROR(TEXT("[IntelGfxEnablePipe] Pipe=%u enable timeout"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxBuildModeProgram(LPGRAPHICSMODEINFO Info, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    U32 RequestedWidth = 0;
    U32 RequestedHeight = 0;
    U32 RequestedBitsPerPixel = 0;

    if (Info == NULL || ProgramOut == NULL) {
        return DF_RETURN_GENERIC;
    }

    RequestedWidth = Info->Width ? Info->Width : IntelGfxState.ActiveWidth;
    RequestedHeight = Info->Height ? Info->Height : IntelGfxState.ActiveHeight;
    RequestedBitsPerPixel = Info->BitsPerPixel ? Info->BitsPerPixel : 32;

    if (RequestedWidth == 0 || RequestedHeight == 0) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (RequestedWidth > IntelGfxState.Capabilities.MaxWidth || RequestedHeight > IntelGfxState.Capabilities.MaxHeight) {
        WARNING(TEXT("[IntelGfxBuildModeProgram] Requested mode outside capabilities (%ux%u max=%ux%u)"),
            RequestedWidth,
            RequestedHeight,
            IntelGfxState.Capabilities.MaxWidth,
            IntelGfxState.Capabilities.MaxHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (RequestedBitsPerPixel != 32) {
        WARNING(TEXT("[IntelGfxBuildModeProgram] Unsupported pixel format bpp=%u"), RequestedBitsPerPixel);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (RequestedWidth != IntelGfxState.ActiveWidth || RequestedHeight != IntelGfxState.ActiveHeight) {
        WARNING(TEXT("[IntelGfxBuildModeProgram] Conservative path supports active mode only (%ux%u requested=%ux%u)"),
            IntelGfxState.ActiveWidth,
            IntelGfxState.ActiveHeight,
            RequestedWidth,
            RequestedHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (!IntelGfxReadModeProgram(IntelGfxState.ActivePipeIndex, ProgramOut)) {
        ERROR(TEXT("[IntelGfxBuildModeProgram] Failed to read active pipe programming"));
        return DF_RETURN_UNEXPECTED;
    }

    ProgramOut->Width = RequestedWidth;
    ProgramOut->Height = RequestedHeight;
    ProgramOut->BitsPerPixel = RequestedBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;
    ProgramOut->PipeSource = ((RequestedHeight - 1) << 16) | (RequestedWidth - 1);
    ProgramOut->PlaneStride = IntelGfxState.ActiveStride;
    ProgramOut->PlaneSurface = IntelGfxState.ActiveSurfaceOffset & INTEL_SURFACE_ALIGN_MASK;
    ProgramOut->PlaneControl &= ~INTEL_PLANE_CTL_FORMAT_MASK;
    ProgramOut->PlaneControl |= INTEL_PLANE_CTL_FORMAT_XRGB8888;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramMode(LPINTEL_GFX_MODE_PROGRAM Program) {
    UINT Result = DF_RETURN_SUCCESS;

    Result = IntelGfxDisablePipe(Program->PipeIndex);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Result = IntelGfxEnablePipe(Program);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    DEBUG(TEXT("[IntelGfxProgramMode] Pipe=%u Mode=%ux%u bpp=%u refresh=%u"),
        Program->PipeIndex,
        Program->Width,
        Program->Height,
        Program->BitsPerPixel,
        Program->RefreshRate);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static BOOL IntelGfxMapActiveFrameBuffer(void) {
    U32 Bar2Raw = 0;
    U32 Bar2Base = 0;
    U32 Bar2Size = 0;

    if (IntelGfxState.Device == NULL) {
        return FALSE;
    }

    Bar2Raw = IntelGfxState.Device->Info.BAR[2];
    if (PCI_BAR_IS_IO(Bar2Raw)) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] BAR2 is I/O (bar2=%x)"), Bar2Raw);
        return FALSE;
    }

    Bar2Base = PCI_GetBARBase(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    Bar2Size = PCI_GetBARSize(IntelGfxState.Device->Info.Bus, IntelGfxState.Device->Info.Dev, IntelGfxState.Device->Info.Func, 2);
    if (Bar2Base == 0 || Bar2Size == 0) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] Invalid BAR2 base=%x size=%u"), Bar2Base, Bar2Size);
        return FALSE;
    }

    IntelGfxState.FrameBufferSize = IntelGfxState.ActiveStride * IntelGfxState.ActiveHeight;
    if (IntelGfxState.FrameBufferSize == 0) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] Invalid frame buffer size"));
        return FALSE;
    }

    if (IntelGfxState.ActiveSurfaceOffset >= Bar2Size) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] Surface offset out of BAR2 range (offset=%x size=%u)"),
            IntelGfxState.ActiveSurfaceOffset,
            Bar2Size);
        return FALSE;
    }

    if (IntelGfxState.FrameBufferSize > (Bar2Size - IntelGfxState.ActiveSurfaceOffset)) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] Frame buffer exceeds BAR2 window (size=%u available=%u)"),
            IntelGfxState.FrameBufferSize,
            Bar2Size - IntelGfxState.ActiveSurfaceOffset);
        return FALSE;
    }

    IntelGfxState.FrameBufferPhysical = (PHYSICAL)(Bar2Base + IntelGfxState.ActiveSurfaceOffset);
    IntelGfxState.FrameBufferLinear = MapIOMemory(IntelGfxState.FrameBufferPhysical, IntelGfxState.FrameBufferSize);
    if (IntelGfxState.FrameBufferLinear == 0) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] MapIOMemory failed for base=%p size=%u"),
            (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical,
            IntelGfxState.FrameBufferSize);
        return FALSE;
    }

    DEBUG(TEXT("[IntelGfxMapActiveFrameBuffer] FrameBuffer=%p size=%u stride=%u"),
        (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical,
        IntelGfxState.FrameBufferSize,
        IntelGfxState.ActiveStride);

    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxBuildTakeoverContext(void) {
    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.ActiveWidth == 0 || IntelGfxState.ActiveHeight == 0) {
        return FALSE;
    }

    IntelGfxState.Context = (GRAPHICSCONTEXT){
        .TypeID = KOID_GRAPHICSCONTEXT,
        .References = 1,
        .Mutex = EMPTY_MUTEX,
        .Driver = &IntelGfxDriver,
        .Width = (I32)IntelGfxState.ActiveWidth,
        .Height = (I32)IntelGfxState.ActiveHeight,
        .BitsPerPixel = IntelGfxState.ActiveBitsPerPixel,
        .BytesPerScanLine = IntelGfxState.ActiveStride,
        .MemoryBase = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear,
        .LoClip = {.X = 0, .Y = 0},
        .HiClip = {.X = (I32)IntelGfxState.ActiveWidth - 1, .Y = (I32)IntelGfxState.ActiveHeight - 1},
        .Origin = {.X = 0, .Y = 0},
        .RasterOperation = ROP_SET
    };

    return TRUE;
}

/************************************************************************/

UINT IntelGfxTakeoverActiveMode(void) {
    if (!IntelGfxReadActiveScanoutState()) {
        ERROR(TEXT("[IntelGfxTakeoverActiveMode] No active Intel scanout state found"));
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxMapActiveFrameBuffer()) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxBuildTakeoverContext()) {
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxSetMode(LPGRAPHICSMODEINFO Info) {
    INTEL_GFX_MODE_PROGRAM Program;
    UINT Result = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    Result = IntelGfxBuildModeProgram(Info, &Program);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Result = IntelGfxProgramMode(&Program);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    IntelGfxReleaseAllSurfaces();
    IntelGfxState.PresentBlitCount = 0;

    if (IntelGfxState.FrameBufferLinear != 0 && IntelGfxState.FrameBufferSize != 0) {
        UnMapIOMemory(IntelGfxState.FrameBufferLinear, IntelGfxState.FrameBufferSize);
        IntelGfxState.FrameBufferLinear = 0;
        IntelGfxState.FrameBufferSize = 0;
        IntelGfxState.FrameBufferPhysical = 0;
    }

    Result = IntelGfxTakeoverActiveMode();
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    SAFE_USE(Info) {
        Info->Width = (U32)IntelGfxState.Context.Width;
        Info->Height = (U32)IntelGfxState.Context.Height;
        Info->BitsPerPixel = IntelGfxState.Context.BitsPerPixel;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
