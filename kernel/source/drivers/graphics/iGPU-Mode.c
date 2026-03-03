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

#define INTEL_MODESET_STAGE_DISABLE_PIPE (1 << 0)
#define INTEL_MODESET_STAGE_ROUTE_TRANSCODER (1 << 1)
#define INTEL_MODESET_STAGE_PROGRAM_CLOCK (1 << 2)
#define INTEL_MODESET_STAGE_CONFIGURE_LINK (1 << 3)
#define INTEL_MODESET_STAGE_ENABLE_PIPE (1 << 4)
#define INTEL_MODESET_STAGE_PANEL_STABILITY (1 << 5)
#define INTEL_MODESET_HBLANK_EXTRA 160
#define INTEL_MODESET_HSYNC_START_OFFSET 48
#define INTEL_MODESET_HSYNC_PULSE_WIDTH 32
#define INTEL_MODESET_VBLANK_EXTRA 30
#define INTEL_MODESET_VSYNC_START_OFFSET 3
#define INTEL_MODESET_VSYNC_PULSE_WIDTH 5

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
static const U32 IntelDdiBufferControlRegisters[] = {
    INTEL_REG_DDI_BUF_CTL_A,
    INTEL_REG_DDI_BUF_CTL_B,
    INTEL_REG_DDI_BUF_CTL_C,
    INTEL_REG_DDI_BUF_CTL_D,
    INTEL_REG_DDI_BUF_CTL_E
};
static const U32 IntelTranscoderDdiRegisters[] = {INTEL_REG_TRANS_DDI_FUNC_CTL_A, INTEL_REG_TRANS_DDI_FUNC_CTL_B, INTEL_REG_TRANS_DDI_FUNC_CTL_C};
static const U32 IntelPortMaskByIndex[] = {INTEL_PORT_A, INTEL_PORT_B, INTEL_PORT_C, INTEL_PORT_D, INTEL_PORT_E};

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

static BOOL IntelGfxReadRegister32Safe(U32 RegisterOffset, U32* ValueOut) {
    if (ValueOut == NULL || RegisterOffset == 0) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(RegisterOffset, ValueOut)) {
        return FALSE;
    }

    return (*ValueOut != 0xFFFFFFFF) ? TRUE : FALSE;
}

/************************************************************************/

static BOOL IntelGfxWriteVerifyRegister32(U32 RegisterOffset, U32 Value, U32 Mask) {
    U32 ReadBack = 0;

    if (RegisterOffset == 0) {
        return TRUE;
    }

    if (!IntelGfxWriteMmio32(RegisterOffset, Value)) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(RegisterOffset, &ReadBack)) {
        return FALSE;
    }

    return ((ReadBack & Mask) == (Value & Mask)) ? TRUE : FALSE;
}

/************************************************************************/

static U32 IntelGfxPortMaskFromIndex(U32 PortIndex) {
    if (PortIndex >= sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0])) {
        return 0;
    }

    return IntelPortMaskByIndex[PortIndex];
}

/************************************************************************/

static BOOL IntelGfxPortIndexFromMask(U32 PortMask, U32* PortIndexOut) {
    U32 Index = 0;

    if (PortIndexOut == NULL || PortMask == 0) {
        return FALSE;
    }

    for (Index = 0; Index < sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0]); Index++) {
        if (IntelPortMaskByIndex[Index] == PortMask) {
            *PortIndexOut = Index;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static U32 IntelGfxFindFirstPortFromMask(U32 PortMask) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelPortMaskByIndex) / sizeof(IntelPortMaskByIndex[0]); Index++) {
        if ((PortMask & IntelPortMaskByIndex[Index]) != 0) {
            return IntelPortMaskByIndex[Index];
        }
    }

    return 0;
}

/************************************************************************/

static U32 IntelGfxResolveOutputTypeFromPort(U32 PortMask) {
    if (PortMask == INTEL_PORT_A) {
        return GFX_OUTPUT_TYPE_EDP;
    }

    if (PortMask == INTEL_PORT_B || PortMask == INTEL_PORT_C || PortMask == INTEL_PORT_D) {
        return GFX_OUTPUT_TYPE_DISPLAYPORT;
    }

    if (PortMask == INTEL_PORT_E) {
        return GFX_OUTPUT_TYPE_HDMI;
    }

    return GFX_OUTPUT_TYPE_UNKNOWN;
}

/************************************************************************/

static U32 IntelGfxFindActiveOutputPortMask(void) {
    U32 Index = 0;

    for (Index = 0; Index < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0]); Index++) {
        U32 Value = 0;

        if (!IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[Index], &Value)) {
            continue;
        }

        if ((Value & INTEL_DDI_BUF_CTL_ENABLE) == 0) {
            continue;
        }

        return IntelGfxPortMaskFromIndex(Index);
    }

    return 0;
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
        U32 ActivePortMask = 0;

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

        ActivePortMask = IntelGfxFindActiveOutputPortMask();
        if (ActivePortMask == 0) {
            ActivePortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
        }

        IntelGfxState.ActivePipeIndex = Index;
        IntelGfxState.ActiveWidth = Width;
        IntelGfxState.ActiveHeight = Height;
        IntelGfxState.ActiveBitsPerPixel = BitsPerPixel;
        IntelGfxState.ActiveStride = Stride;
        IntelGfxState.ActiveSurfaceOffset = PlaneSurface & INTEL_SURFACE_ALIGN_MASK;
        IntelGfxState.ActiveOutputPortMask = ActivePortMask;
        IntelGfxState.ActiveTranscoderIndex = Index;
        IntelGfxState.HasActiveMode = TRUE;

        DEBUG(TEXT("[IntelGfxReadActiveScanoutState] Pipe=%u Width=%u Height=%u Bpp=%u Stride=%u Surface=%x Port=%x"),
            Index,
            Width,
            Height,
            BitsPerPixel,
            Stride,
            IntelGfxState.ActiveSurfaceOffset,
            ActivePortMask);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL IntelGfxReadModeProgram(U32 PipeIndex, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    U32 OutputPortMask = 0;

    if (ProgramOut == NULL || PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    ProgramOut->PipeIndex = PipeIndex;
    ProgramOut->Width = IntelGfxState.ActiveWidth;
    ProgramOut->Height = IntelGfxState.ActiveHeight;
    ProgramOut->BitsPerPixel = IntelGfxState.ActiveBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;

    if (!(IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &ProgramOut->PipeConf) &&
            IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &ProgramOut->PipeSource) &&
            IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &ProgramOut->PipeHTotal) &&
            IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &ProgramOut->PipeHBlank) &&
            IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &ProgramOut->PipeHSync) &&
            IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &ProgramOut->PipeVTotal) &&
            IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &ProgramOut->PipeVBlank) &&
            IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &ProgramOut->PipeVSync) &&
            IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &ProgramOut->PlaneControl) &&
            IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &ProgramOut->PlaneStride) &&
            IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &ProgramOut->PlaneSurface))) {
        return FALSE;
    }

    OutputPortMask = IntelGfxFindActiveOutputPortMask();
    if (OutputPortMask == 0) {
        OutputPortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
    }

    ProgramOut->OutputPortMask = OutputPortMask;
    ProgramOut->OutputType = IntelGfxResolveOutputTypeFromPort(OutputPortMask);
    ProgramOut->TranscoderIndex = (PipeIndex < IntelGfxState.IntelCapabilities.TranscoderCount) ? PipeIndex : 0;

    return TRUE;
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

static BOOL IntelGfxVerifyPipeEnabledState(U32 PipeIndex, BOOL ExpectedPipeEnabled, BOOL ExpectedPlaneEnabled) {
    U32 PipeConf = 0;
    U32 PlaneControl = 0;
    BOOL PipeEnabled = FALSE;
    BOOL PlaneEnabled = FALSE;

    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &PipeConf)) {
        return FALSE;
    }

    if (!IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &PlaneControl)) {
        return FALSE;
    }

    PipeEnabled = (PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
    PlaneEnabled = (PlaneControl & INTEL_PLANE_CTL_ENABLE) ? TRUE : FALSE;

    return (PipeEnabled == ExpectedPipeEnabled && PlaneEnabled == ExpectedPlaneEnabled) ? TRUE : FALSE;
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

    if (!IntelGfxVerifyPipeEnabledState(PipeIndex, FALSE, FALSE)) {
        ERROR(TEXT("[IntelGfxDisablePipe] Pipe=%u disable verification failed"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramTranscoderRoute(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 PortIndex = 0;
    U32 TranscoderControl = 0;
    U32 RegisterOffset = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (Program->TranscoderIndex >= sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0])) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxPortIndexFromMask(Program->OutputPortMask, &PortIndex)) {
        return DF_RETURN_UNEXPECTED;
    }

    RegisterOffset = IntelTranscoderDdiRegisters[Program->TranscoderIndex];
    if (!IntelGfxReadRegister32Safe(RegisterOffset, &TranscoderControl)) {
        WARNING(TEXT("[IntelGfxProgramTranscoderRoute] Transcoder register unavailable (transcoder=%u)"), Program->TranscoderIndex);
        return DF_RETURN_SUCCESS;
    }

    TranscoderControl &= ~INTEL_TRANS_DDI_FUNC_PORT_MASK;
    TranscoderControl |= (PortIndex << INTEL_TRANS_DDI_FUNC_PORT_SHIFT);
    TranscoderControl |= INTEL_TRANS_DDI_FUNC_ENABLE;

    if (!IntelGfxWriteVerifyRegister32(RegisterOffset, TranscoderControl, INTEL_TRANS_DDI_FUNC_PORT_MASK | INTEL_TRANS_DDI_FUNC_ENABLE)) {
        ERROR(TEXT("[IntelGfxProgramTranscoderRoute] Transcoder route write failed (transcoder=%u port=%x)"),
            Program->TranscoderIndex,
            Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    Program->TranscoderControl = TranscoderControl;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramClockSource(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 ClockControl = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->ClockControlRegister = 0;
    Program->ClockControlValue = 0;

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxReadRegister32Safe(INTEL_REG_DPLL_CTRL1, &ClockControl)) {
        WARNING(TEXT("[IntelGfxProgramClockSource] DPLL control register unavailable"));
        return DF_RETURN_SUCCESS;
    }

    Program->ClockControlRegister = INTEL_REG_DPLL_CTRL1;
    Program->ClockControlValue = ClockControl;

    // Conservative multi-family path: preserve active DPLL source selection.
    if (!IntelGfxWriteVerifyRegister32(Program->ClockControlRegister, Program->ClockControlValue, MAX_U32)) {
        ERROR(TEXT("[IntelGfxProgramClockSource] DPLL programming failed"));
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxConfigureConnectorLink(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 PortIndex = 0;
    U32 LinkControl = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.IntelCapabilities.DisplayVersion < 9) {
        return DF_RETURN_SUCCESS;
    }

    if (!IntelGfxPortIndexFromMask(Program->OutputPortMask, &PortIndex)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (PortIndex >= sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->LinkControlRegister = IntelDdiBufferControlRegisters[PortIndex];
    if (!IntelGfxReadRegister32Safe(Program->LinkControlRegister, &LinkControl)) {
        ERROR(TEXT("[IntelGfxConfigureConnectorLink] DDI link register unavailable for port=%x"), Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    LinkControl |= INTEL_DDI_BUF_CTL_ENABLE;

    if (!IntelGfxWriteVerifyRegister32(Program->LinkControlRegister, LinkControl, INTEL_DDI_BUF_CTL_ENABLE)) {
        ERROR(TEXT("[IntelGfxConfigureConnectorLink] Link enable failed for port=%x"), Program->OutputPortMask);
        return DF_RETURN_UNEXPECTED;
    }

    Program->LinkControlValue = LinkControl;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramPanelStability(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 Value = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Program->PanelPowerRegister = 0;
    Program->PanelPowerValue = 0;
    Program->BacklightRegister = 0;
    Program->BacklightValue = 0;

    if (Program->OutputType != GFX_OUTPUT_TYPE_EDP) {
        return DF_RETURN_SUCCESS;
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_PP_CONTROL, &Value)) {
        Program->PanelPowerRegister = INTEL_REG_PP_CONTROL;
        Program->PanelPowerValue = Value | INTEL_PANEL_POWER_TARGET_ON;

        if (!IntelGfxWriteVerifyRegister32(Program->PanelPowerRegister, Program->PanelPowerValue, INTEL_PANEL_POWER_TARGET_ON)) {
            ERROR(TEXT("[IntelGfxProgramPanelStability] Panel power target programming failed"));
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_BLC_PWM_CTL2, &Value)) {
        Program->BacklightRegister = INTEL_REG_BLC_PWM_CTL2;
        Program->BacklightValue = Value | INTEL_BACKLIGHT_PWM_ENABLE;

        if (!IntelGfxWriteVerifyRegister32(Program->BacklightRegister, Program->BacklightValue, INTEL_BACKLIGHT_PWM_ENABLE)) {
            ERROR(TEXT("[IntelGfxProgramPanelStability] Backlight programming failed"));
            return DF_RETURN_UNEXPECTED;
        }
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

    if (!IntelGfxWriteVerifyRegister32(IntelPipeHTotalRegisters[PipeIndex], Program->PipeHTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHBlankRegisters[PipeIndex], Program->PipeHBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHSyncRegisters[PipeIndex], Program->PipeHSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVTotalRegisters[PipeIndex], Program->PipeVTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVBlankRegisters[PipeIndex], Program->PipeVBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVSyncRegisters[PipeIndex], Program->PipeVSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeSourceRegisters[PipeIndex], Program->PipeSource, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneStrideRegisters[PipeIndex], Program->PlaneStride, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneSurfaceRegisters[PipeIndex], Program->PlaneSurface, INTEL_SURFACE_ALIGN_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeConf = Program->PipeConf | INTEL_PIPE_CONF_ENABLE;
    if (!IntelGfxWriteVerifyRegister32(IntelPipeConfRegisters[PipeIndex], PipeConf, INTEL_PIPE_CONF_ENABLE)) {
        return DF_RETURN_UNEXPECTED;
    }

    PlaneControl = Program->PlaneControl;
    PlaneControl &= ~INTEL_PLANE_CTL_FORMAT_MASK;
    PlaneControl |= INTEL_PLANE_CTL_FORMAT_XRGB8888;
    PlaneControl |= INTEL_PLANE_CTL_ENABLE;
    if (!IntelGfxWriteVerifyRegister32(
            IntelPlaneControlRegisters[PipeIndex], PlaneControl, INTEL_PLANE_CTL_ENABLE | INTEL_PLANE_CTL_FORMAT_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWaitPipeState(PipeIndex, TRUE)) {
        ERROR(TEXT("[IntelGfxEnablePipe] Pipe=%u enable timeout"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxVerifyPipeEnabledState(PipeIndex, TRUE, TRUE)) {
        ERROR(TEXT("[IntelGfxEnablePipe] Pipe=%u enable verification failed"), PipeIndex);
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxSelectPipeOutputRouting(LPINTEL_GFX_MODE_PROGRAM Program) {
    U32 AvailablePortMask = 0;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    AvailablePortMask = IntelGfxState.IntelCapabilities.PortMask;

    if (IntelGfxState.ActiveOutputPortMask != 0 && (IntelGfxState.ActiveOutputPortMask & AvailablePortMask) != 0) {
        Program->OutputPortMask = IntelGfxState.ActiveOutputPortMask;
    } else if ((INTEL_PORT_A & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_A;
    } else if ((INTEL_PORT_B & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_B;
    } else if ((INTEL_PORT_C & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_C;
    } else if ((INTEL_PORT_D & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_D;
    } else if ((INTEL_PORT_E & AvailablePortMask) != 0) {
        Program->OutputPortMask = INTEL_PORT_E;
    } else {
        return DF_RETURN_UNEXPECTED;
    }

    Program->OutputType = IntelGfxResolveOutputTypeFromPort(Program->OutputPortMask);
    Program->TranscoderIndex = Program->PipeIndex;
    if (Program->TranscoderIndex >= IntelGfxState.IntelCapabilities.TranscoderCount) {
        Program->TranscoderIndex = IntelGfxState.IntelCapabilities.TranscoderCount ? (IntelGfxState.IntelCapabilities.TranscoderCount - 1) : 0;
    }

    DEBUG(TEXT("[IntelGfxSelectPipeOutputRouting] Pipe=%u OutputPort=%x OutputType=%u Transcoder=%u"),
        Program->PipeIndex,
        Program->OutputPortMask,
        Program->OutputType,
        Program->TranscoderIndex);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxCaptureModeSnapshot(U32 PipeIndex, LPINTEL_GFX_MODE_SNAPSHOT SnapshotOut) {
    U32 PortIndex = 0;

    if (SnapshotOut == NULL || PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    *SnapshotOut = (INTEL_GFX_MODE_SNAPSHOT){0};
    SnapshotOut->PipeIndex = PipeIndex;

    if (!(IntelGfxReadMmio32(IntelPipeConfRegisters[PipeIndex], &SnapshotOut->PipeConf) &&
            IntelGfxReadMmio32(IntelPipeSourceRegisters[PipeIndex], &SnapshotOut->PipeSource) &&
            IntelGfxReadMmio32(IntelPipeHTotalRegisters[PipeIndex], &SnapshotOut->PipeHTotal) &&
            IntelGfxReadMmio32(IntelPipeHBlankRegisters[PipeIndex], &SnapshotOut->PipeHBlank) &&
            IntelGfxReadMmio32(IntelPipeHSyncRegisters[PipeIndex], &SnapshotOut->PipeHSync) &&
            IntelGfxReadMmio32(IntelPipeVTotalRegisters[PipeIndex], &SnapshotOut->PipeVTotal) &&
            IntelGfxReadMmio32(IntelPipeVBlankRegisters[PipeIndex], &SnapshotOut->PipeVBlank) &&
            IntelGfxReadMmio32(IntelPipeVSyncRegisters[PipeIndex], &SnapshotOut->PipeVSync) &&
            IntelGfxReadMmio32(IntelPlaneControlRegisters[PipeIndex], &SnapshotOut->PlaneControl) &&
            IntelGfxReadMmio32(IntelPlaneStrideRegisters[PipeIndex], &SnapshotOut->PlaneStride) &&
            IntelGfxReadMmio32(IntelPlaneSurfaceRegisters[PipeIndex], &SnapshotOut->PlaneSurface))) {
        return DF_RETURN_UNEXPECTED;
    }

    SnapshotOut->OutputPortMask = IntelGfxState.ActiveOutputPortMask;
    if (SnapshotOut->OutputPortMask != 0 && IntelGfxPortIndexFromMask(SnapshotOut->OutputPortMask, &PortIndex)) {
        if (PortIndex < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
            if (IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[PortIndex], &SnapshotOut->LinkControlValue)) {
                // Captured for rollback.
            }
        }
    }

    if (IntelGfxState.ActiveTranscoderIndex < sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0])) {
        (void)IntelGfxReadRegister32Safe(
            IntelTranscoderDdiRegisters[IntelGfxState.ActiveTranscoderIndex], &SnapshotOut->TranscoderControl);
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_DPLL_CTRL1, &SnapshotOut->ClockControlValue)) {
        SnapshotOut->ClockControlRegister = INTEL_REG_DPLL_CTRL1;
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_PP_CONTROL, &SnapshotOut->PanelPowerValue)) {
        SnapshotOut->PanelPowerRegister = INTEL_REG_PP_CONTROL;
    }

    if (IntelGfxReadRegister32Safe(INTEL_REG_BLC_PWM_CTL2, &SnapshotOut->BacklightValue)) {
        SnapshotOut->BacklightRegister = INTEL_REG_BLC_PWM_CTL2;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxRestoreModeSnapshot(LPINTEL_GFX_MODE_SNAPSHOT Snapshot) {
    U32 PipeIndex = 0;
    U32 PortIndex = 0;
    BOOL ExpectedEnabled = FALSE;

    if (Snapshot == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    PipeIndex = Snapshot->PipeIndex;
    if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
        return DF_RETURN_UNEXPECTED;
    }

    (void)IntelGfxDisablePipe(PipeIndex);

    if (!IntelGfxWriteVerifyRegister32(IntelPipeHTotalRegisters[PipeIndex], Snapshot->PipeHTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHBlankRegisters[PipeIndex], Snapshot->PipeHBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeHSyncRegisters[PipeIndex], Snapshot->PipeHSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVTotalRegisters[PipeIndex], Snapshot->PipeVTotal, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVBlankRegisters[PipeIndex], Snapshot->PipeVBlank, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeVSyncRegisters[PipeIndex], Snapshot->PipeVSync, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPipeSourceRegisters[PipeIndex], Snapshot->PipeSource, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneStrideRegisters[PipeIndex], Snapshot->PlaneStride, MAX_U32) ||
        !IntelGfxWriteVerifyRegister32(IntelPlaneSurfaceRegisters[PipeIndex], Snapshot->PlaneSurface, INTEL_SURFACE_ALIGN_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Snapshot->OutputPortMask != 0 && IntelGfxPortIndexFromMask(Snapshot->OutputPortMask, &PortIndex) &&
        PortIndex < sizeof(IntelDdiBufferControlRegisters) / sizeof(IntelDdiBufferControlRegisters[0])) {
        U32 LinkValue = 0;
        if (IntelGfxReadRegister32Safe(IntelDdiBufferControlRegisters[PortIndex], &LinkValue)) {
            if (!IntelGfxWriteVerifyRegister32(
                    IntelDdiBufferControlRegisters[PortIndex], Snapshot->LinkControlValue, INTEL_DDI_BUF_CTL_ENABLE)) {
                return DF_RETURN_UNEXPECTED;
            }
        }
    }

    if (IntelGfxState.ActiveTranscoderIndex < sizeof(IntelTranscoderDdiRegisters) / sizeof(IntelTranscoderDdiRegisters[0]) &&
        Snapshot->TranscoderControl != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                IntelTranscoderDdiRegisters[IntelGfxState.ActiveTranscoderIndex],
                Snapshot->TranscoderControl,
                INTEL_TRANS_DDI_FUNC_ENABLE | INTEL_TRANS_DDI_FUNC_PORT_MASK)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->ClockControlRegister != 0 && Snapshot->ClockControlValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(Snapshot->ClockControlRegister, Snapshot->ClockControlValue, MAX_U32)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->PanelPowerRegister != 0 && Snapshot->PanelPowerValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                Snapshot->PanelPowerRegister, Snapshot->PanelPowerValue, INTEL_PANEL_POWER_TARGET_ON)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (Snapshot->BacklightRegister != 0 && Snapshot->BacklightValue != 0) {
        if (!IntelGfxWriteVerifyRegister32(
                Snapshot->BacklightRegister, Snapshot->BacklightValue, INTEL_BACKLIGHT_PWM_ENABLE)) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (!IntelGfxWriteVerifyRegister32(IntelPipeConfRegisters[PipeIndex], Snapshot->PipeConf, INTEL_PIPE_CONF_ENABLE)) {
        return DF_RETURN_UNEXPECTED;
    }

    if (!IntelGfxWriteVerifyRegister32(
            IntelPlaneControlRegisters[PipeIndex],
            Snapshot->PlaneControl,
            INTEL_PLANE_CTL_ENABLE | INTEL_PLANE_CTL_FORMAT_MASK)) {
        return DF_RETURN_UNEXPECTED;
    }

    ExpectedEnabled = (Snapshot->PipeConf & INTEL_PIPE_CONF_ENABLE) ? TRUE : FALSE;
    if (!IntelGfxWaitPipeState(PipeIndex, ExpectedEnabled)) {
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxBuildModeProgram(LPGRAPHICSMODEINFO Info, LPINTEL_GFX_MODE_PROGRAM ProgramOut) {
    U32 RequestedWidth = 0;
    U32 RequestedHeight = 0;
    U32 RequestedBitsPerPixel = 0;
    BOOL HasActiveMode = FALSE;

    if (Info == NULL || ProgramOut == NULL) {
        return DF_RETURN_GENERIC;
    }

    RequestedWidth = Info->Width ? Info->Width : IntelGfxState.ActiveWidth;
    RequestedHeight = Info->Height ? Info->Height : IntelGfxState.ActiveHeight;
    RequestedBitsPerPixel = Info->BitsPerPixel ? Info->BitsPerPixel : 32;
    HasActiveMode = IntelGfxState.HasActiveMode;

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

    if (HasActiveMode != FALSE && (RequestedWidth != IntelGfxState.ActiveWidth || RequestedHeight != IntelGfxState.ActiveHeight)) {
        WARNING(TEXT("[IntelGfxBuildModeProgram] Conservative path supports active mode only (%ux%u requested=%ux%u)"),
            IntelGfxState.ActiveWidth,
            IntelGfxState.ActiveHeight,
            RequestedWidth,
            RequestedHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (HasActiveMode != FALSE) {
        if (!IntelGfxReadModeProgram(IntelGfxState.ActivePipeIndex, ProgramOut)) {
            ERROR(TEXT("[IntelGfxBuildModeProgram] Failed to read active pipe programming"));
            return DF_RETURN_UNEXPECTED;
        }
    } else {
        U32 PipeIndex = 0;
        U32 HorizontalTotal = 0;
        U32 HorizontalBlankStart = 0;
        U32 HorizontalSyncStart = 0;
        U32 HorizontalSyncEnd = 0;
        U32 VerticalTotal = 0;
        U32 VerticalBlankStart = 0;
        U32 VerticalSyncStart = 0;
        U32 VerticalSyncEnd = 0;

        ProgramOut->PipeIndex = 0;
        if (IntelGfxState.IntelCapabilities.PipeCount != 0 && ProgramOut->PipeIndex >= IntelGfxState.IntelCapabilities.PipeCount) {
            ProgramOut->PipeIndex = IntelGfxState.IntelCapabilities.PipeCount - 1;
        }

        PipeIndex = ProgramOut->PipeIndex;
        if (PipeIndex >= sizeof(IntelPipeConfRegisters) / sizeof(IntelPipeConfRegisters[0])) {
            return DF_RETURN_UNEXPECTED;
        }

        ProgramOut->PipeConf = 0;
        ProgramOut->PipeSource = ((RequestedHeight - 1) << 16) | (RequestedWidth - 1);
        ProgramOut->PlaneControl = INTEL_PLANE_CTL_FORMAT_XRGB8888;
        ProgramOut->PlaneStride = RequestedWidth << 2;
        ProgramOut->PlaneSurface = 0;
        ProgramOut->OutputPortMask = IntelGfxFindFirstPortFromMask(IntelGfxState.IntelCapabilities.PortMask);
        ProgramOut->OutputType = IntelGfxResolveOutputTypeFromPort(ProgramOut->OutputPortMask);
        ProgramOut->TranscoderIndex = 0;

        HorizontalTotal = RequestedWidth + INTEL_MODESET_HBLANK_EXTRA;
        HorizontalBlankStart = RequestedWidth;
        HorizontalSyncStart = HorizontalBlankStart + INTEL_MODESET_HSYNC_START_OFFSET;
        HorizontalSyncEnd = HorizontalSyncStart + INTEL_MODESET_HSYNC_PULSE_WIDTH;
        VerticalTotal = RequestedHeight + INTEL_MODESET_VBLANK_EXTRA;
        VerticalBlankStart = RequestedHeight;
        VerticalSyncStart = VerticalBlankStart + INTEL_MODESET_VSYNC_START_OFFSET;
        VerticalSyncEnd = VerticalSyncStart + INTEL_MODESET_VSYNC_PULSE_WIDTH;

        ProgramOut->PipeHTotal = ((HorizontalTotal - 1) << 16) | (RequestedWidth - 1);
        ProgramOut->PipeHBlank = ((HorizontalTotal - 1) << 16) | (HorizontalBlankStart - 1);
        ProgramOut->PipeHSync = ((HorizontalSyncEnd - 1) << 16) | (HorizontalSyncStart - 1);
        ProgramOut->PipeVTotal = ((VerticalTotal - 1) << 16) | (RequestedHeight - 1);
        ProgramOut->PipeVBlank = ((VerticalTotal - 1) << 16) | (VerticalBlankStart - 1);
        ProgramOut->PipeVSync = ((VerticalSyncEnd - 1) << 16) | (VerticalSyncStart - 1);

        DEBUG(TEXT("[IntelGfxBuildModeProgram] Cold modeset bootstrap prepared (%ux%u pipe=%u port=%x)"),
            RequestedWidth,
            RequestedHeight,
            ProgramOut->PipeIndex,
            ProgramOut->OutputPortMask);
    }

    ProgramOut->Width = RequestedWidth;
    ProgramOut->Height = RequestedHeight;
    ProgramOut->BitsPerPixel = RequestedBitsPerPixel;
    ProgramOut->RefreshRate = INTEL_DEFAULT_REFRESH_RATE;
    ProgramOut->PipeSource = ((RequestedHeight - 1) << 16) | (RequestedWidth - 1);
    if (HasActiveMode != FALSE) {
        ProgramOut->PlaneStride = IntelGfxState.ActiveStride;
        ProgramOut->PlaneSurface = IntelGfxState.ActiveSurfaceOffset & INTEL_SURFACE_ALIGN_MASK;
    }
    ProgramOut->PlaneControl &= ~INTEL_PLANE_CTL_FORMAT_MASK;
    ProgramOut->PlaneControl |= INTEL_PLANE_CTL_FORMAT_XRGB8888;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxProgramMode(LPINTEL_GFX_MODE_PROGRAM Program) {
    INTEL_GFX_MODE_SNAPSHOT Snapshot;
    UINT CompletedStages = 0;
    UINT Result = DF_RETURN_SUCCESS;
    BOOL HasSnapshot = FALSE;

    if (Program == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    Result = IntelGfxCaptureModeSnapshot(Program->PipeIndex, &Snapshot);
    if (Result != DF_RETURN_SUCCESS) {
        if (IntelGfxState.HasActiveMode != FALSE) {
            ERROR(TEXT("[IntelGfxProgramMode] Snapshot capture failed"));
            return Result;
        }

        WARNING(TEXT("[IntelGfxProgramMode] Snapshot capture unavailable, continuing without rollback baseline"));
    } else {
        HasSnapshot = TRUE;
    }

    Result = IntelGfxSelectPipeOutputRouting(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Routing policy failed"));
        return Result;
    }

    Result = IntelGfxDisablePipe(Program->PipeIndex);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage disable failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_DISABLE_PIPE;

    Result = IntelGfxProgramTranscoderRoute(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage transcoder routing failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_ROUTE_TRANSCODER;

    Result = IntelGfxProgramClockSource(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage clock programming failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_PROGRAM_CLOCK;

    Result = IntelGfxConfigureConnectorLink(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage link configuration failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_CONFIGURE_LINK;

    Result = IntelGfxEnablePipe(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage pipe enable failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_ENABLE_PIPE;

    Result = IntelGfxProgramPanelStability(Program);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[IntelGfxProgramMode] Stage panel stabilization failed"));
        goto rollback;
    }
    CompletedStages |= INTEL_MODESET_STAGE_PANEL_STABILITY;

    IntelGfxState.ActiveOutputPortMask = Program->OutputPortMask;
    IntelGfxState.ActiveTranscoderIndex = Program->TranscoderIndex;

    DEBUG(TEXT("[IntelGfxProgramMode] Pipe=%u Mode=%ux%u bpp=%u refresh=%u Port=%x Transcoder=%u Stages=%x"),
        Program->PipeIndex,
        Program->Width,
        Program->Height,
        Program->BitsPerPixel,
        Program->RefreshRate,
        Program->OutputPortMask,
        Program->TranscoderIndex,
        CompletedStages);

    return DF_RETURN_SUCCESS;

rollback:
    if (CompletedStages != 0 && HasSnapshot != FALSE) {
        UINT RollbackResult = IntelGfxRestoreModeSnapshot(&Snapshot);
        if (RollbackResult != DF_RETURN_SUCCESS) {
            ERROR(TEXT("[IntelGfxProgramMode] Rollback failed stageMask=%x result=%u"), CompletedStages, RollbackResult);
        } else {
            WARNING(TEXT("[IntelGfxProgramMode] Rollback completed stageMask=%x"), CompletedStages);
        }
    }

    return Result;
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

static void IntelGfxApplyProgramAsActiveState(const INTEL_GFX_MODE_PROGRAM* Program) {
    if (Program == NULL) {
        return;
    }

    IntelGfxState.ActivePipeIndex = Program->PipeIndex;
    IntelGfxState.ActiveWidth = Program->Width;
    IntelGfxState.ActiveHeight = Program->Height;
    IntelGfxState.ActiveBitsPerPixel = Program->BitsPerPixel;
    IntelGfxState.ActiveStride = Program->PlaneStride;
    IntelGfxState.ActiveSurfaceOffset = Program->PlaneSurface & INTEL_SURFACE_ALIGN_MASK;
    IntelGfxState.ActiveOutputPortMask = Program->OutputPortMask;
    IntelGfxState.ActiveTranscoderIndex = Program->TranscoderIndex;
}

/************************************************************************/

UINT IntelGfxTakeoverActiveMode(void) {
    if (!IntelGfxReadActiveScanoutState()) {
        ERROR(TEXT("[IntelGfxTakeoverActiveMode] No active Intel scanout state found"));
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_NO_ACTIVE_SCANOUT;
    }

    if (!IntelGfxMapActiveFrameBuffer()) {
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_MAP_FRAMEBUFFER_FAILED;
    }

    if (!IntelGfxBuildTakeoverContext()) {
        IntelGfxState.HasActiveMode = FALSE;
        return DF_RETURN_IGFX_BUILD_CONTEXT_FAILED;
    }

    IntelGfxState.HasActiveMode = TRUE;
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
        IntelGfxApplyProgramAsActiveState(&Program);

        Result = IntelGfxMapActiveFrameBuffer() ? DF_RETURN_SUCCESS : DF_RETURN_IGFX_MAP_FRAMEBUFFER_FAILED;
        if (Result != DF_RETURN_SUCCESS) {
            IntelGfxState.HasActiveMode = FALSE;
            return Result;
        }

        Result = IntelGfxBuildTakeoverContext() ? DF_RETURN_SUCCESS : DF_RETURN_IGFX_BUILD_CONTEXT_FAILED;
        if (Result != DF_RETURN_SUCCESS) {
            IntelGfxState.HasActiveMode = FALSE;
            return Result;
        }

        IntelGfxState.HasActiveMode = TRUE;
        WARNING(TEXT("[IntelGfxSetMode] Takeover refresh unavailable, context rebuilt from programmed cold mode"));
    }

    SAFE_USE(Info) {
        Info->Width = (U32)IntelGfxState.Context.Width;
        Info->Height = (U32)IntelGfxState.Context.Height;
        Info->BitsPerPixel = IntelGfxState.Context.BitsPerPixel;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
