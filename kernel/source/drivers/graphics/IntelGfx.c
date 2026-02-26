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


    Intel graphics (native skeleton)

\************************************************************************/

#include "GFX.h"
#include "Clock.h"
#include "KernelData.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/graphics/GfxTextRenderer.h"
#include "drivers/bus/PCI.h"

/************************************************************************/

#define INTEL_GFX_VER_MAJOR 1
#define INTEL_GFX_VER_MINOR 0

#define INTEL_VENDOR_ID 0x8086
#define INTEL_MMIO_PROBE_REGISTER 0x0000

#define INTEL_PORT_A (1 << 0)
#define INTEL_PORT_B (1 << 1)
#define INTEL_PORT_C (1 << 2)
#define INTEL_PORT_D (1 << 3)
#define INTEL_PORT_E (1 << 4)

#define INTEL_REG_GMD_ID 0x51000
#define INTEL_REG_PIPE_A_CONF 0x70008
#define INTEL_REG_PIPE_B_CONF 0x71008
#define INTEL_REG_PIPE_C_CONF 0x72008
#define INTEL_REG_PIPE_A_SRC 0x6001C
#define INTEL_REG_PIPE_B_SRC 0x6101C
#define INTEL_REG_PIPE_C_SRC 0x6201C
#define INTEL_REG_PIPE_A_HTOTAL 0x60000
#define INTEL_REG_PIPE_B_HTOTAL 0x61000
#define INTEL_REG_PIPE_C_HTOTAL 0x62000
#define INTEL_REG_PIPE_A_HBLANK 0x60004
#define INTEL_REG_PIPE_B_HBLANK 0x61004
#define INTEL_REG_PIPE_C_HBLANK 0x62004
#define INTEL_REG_PIPE_A_HSYNC 0x60008
#define INTEL_REG_PIPE_B_HSYNC 0x61008
#define INTEL_REG_PIPE_C_HSYNC 0x62008
#define INTEL_REG_PIPE_A_VTOTAL 0x6000C
#define INTEL_REG_PIPE_B_VTOTAL 0x6100C
#define INTEL_REG_PIPE_C_VTOTAL 0x6200C
#define INTEL_REG_PIPE_A_VBLANK 0x60010
#define INTEL_REG_PIPE_B_VBLANK 0x61010
#define INTEL_REG_PIPE_C_VBLANK 0x62010
#define INTEL_REG_PIPE_A_VSYNC 0x60014
#define INTEL_REG_PIPE_B_VSYNC 0x61014
#define INTEL_REG_PIPE_C_VSYNC 0x62014
#define INTEL_REG_PLANE_A_CTL 0x70180
#define INTEL_REG_PLANE_B_CTL 0x71180
#define INTEL_REG_PLANE_C_CTL 0x72180
#define INTEL_REG_PLANE_A_STRIDE 0x70188
#define INTEL_REG_PLANE_B_STRIDE 0x71188
#define INTEL_REG_PLANE_C_STRIDE 0x72188
#define INTEL_REG_PLANE_A_SURF 0x7019C
#define INTEL_REG_PLANE_B_SURF 0x7119C
#define INTEL_REG_PLANE_C_SURF 0x7219C
#define INTEL_REG_DDI_BUF_CTL_A 0x64000
#define INTEL_REG_DDI_BUF_CTL_B 0x64100
#define INTEL_REG_DDI_BUF_CTL_C 0x64200
#define INTEL_REG_DDI_BUF_CTL_D 0x64300
#define INTEL_REG_DDI_BUF_CTL_E 0x64400

#define INTEL_PIPE_CONF_ENABLE (1 << 31)
#define INTEL_PLANE_CTL_ENABLE (1 << 31)
#define INTEL_PLANE_CTL_FORMAT_MASK (0x0F << 24)
#define INTEL_SURFACE_ALIGN_MASK 0xFFFFF000
#define INTEL_PLANE_CTL_FORMAT_XRGB8888 (0x04 << 24)
#define INTEL_MODESET_LOOP_LIMIT 50000
#define INTEL_MODESET_TIMEOUT_MILLISECONDS 50
#define INTEL_DEFAULT_REFRESH_RATE 60

/************************************************************************/

typedef struct tag_INTEL_GFX_CAPS {
    U32 Generation;
    U32 DisplayVersion;
    U32 PipeCount;
    U32 TranscoderCount;
    U32 PortMask;
    BOOL SupportsFBC;
    BOOL SupportsPSR;
    BOOL SupportsAsyncFlip;
    U32 MaxWidth;
    U32 MaxHeight;
} INTEL_GFX_CAPS, *LPINTEL_GFX_CAPS;

/************************************************************************/

typedef struct tag_INTEL_GFX_FAMILY_ENTRY {
    U16 DeviceId;
    U16 DeviceIdMask;
    U32 Generation;
    U32 DisplayVersion;
    U32 PipeCount;
    U32 TranscoderCount;
    U32 PortMask;
    BOOL SupportsFBC;
    BOOL SupportsPSR;
    BOOL SupportsAsyncFlip;
    U32 MaxWidth;
    U32 MaxHeight;
} INTEL_GFX_FAMILY_ENTRY, *LPINTEL_GFX_FAMILY_ENTRY;

/************************************************************************/

static const INTEL_GFX_FAMILY_ENTRY IntelGfxFamilyTable[] = {
    {.DeviceId = 0x0100,
        .DeviceIdMask = 0xFF00,
        .Generation = 6,
        .DisplayVersion = 6,
        .PipeCount = 2,
        .TranscoderCount = 2,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C,
        .SupportsFBC = TRUE,
        .SupportsPSR = FALSE,
        .SupportsAsyncFlip = FALSE,
        .MaxWidth = 4096,
        .MaxHeight = 4096},
    {.DeviceId = 0x1600,
        .DeviceIdMask = 0xFF00,
        .Generation = 8,
        .DisplayVersion = 8,
        .PipeCount = 3,
        .TranscoderCount = 3,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C | INTEL_PORT_D,
        .SupportsFBC = TRUE,
        .SupportsPSR = TRUE,
        .SupportsAsyncFlip = FALSE,
        .MaxWidth = 5120,
        .MaxHeight = 3200},
    {.DeviceId = 0x1900,
        .DeviceIdMask = 0xFF00,
        .Generation = 9,
        .DisplayVersion = 9,
        .PipeCount = 3,
        .TranscoderCount = 3,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C | INTEL_PORT_D,
        .SupportsFBC = TRUE,
        .SupportsPSR = TRUE,
        .SupportsAsyncFlip = FALSE,
        .MaxWidth = 5120,
        .MaxHeight = 3200},
    {.DeviceId = 0x3E00,
        .DeviceIdMask = 0xFF00,
        .Generation = 9,
        .DisplayVersion = 10,
        .PipeCount = 3,
        .TranscoderCount = 3,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C | INTEL_PORT_D,
        .SupportsFBC = TRUE,
        .SupportsPSR = TRUE,
        .SupportsAsyncFlip = TRUE,
        .MaxWidth = 8192,
        .MaxHeight = 8192},
    {.DeviceId = 0x8A00,
        .DeviceIdMask = 0xFF00,
        .Generation = 11,
        .DisplayVersion = 11,
        .PipeCount = 3,
        .TranscoderCount = 4,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C | INTEL_PORT_D | INTEL_PORT_E,
        .SupportsFBC = TRUE,
        .SupportsPSR = TRUE,
        .SupportsAsyncFlip = TRUE,
        .MaxWidth = 8192,
        .MaxHeight = 8192},
    {.DeviceId = 0x9A00,
        .DeviceIdMask = 0xFF00,
        .Generation = 12,
        .DisplayVersion = 12,
        .PipeCount = 4,
        .TranscoderCount = 4,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C | INTEL_PORT_D | INTEL_PORT_E,
        .SupportsFBC = TRUE,
        .SupportsPSR = TRUE,
        .SupportsAsyncFlip = TRUE,
        .MaxWidth = 8192,
        .MaxHeight = 8192}
};

/************************************************************************/

typedef struct tag_INTEL_GFX_STATE {
    LPPCI_DEVICE Device;
    LINEAR MmioBase;
    U32 MmioSize;
    U32 ActivePipeIndex;
    U32 ActiveWidth;
    U32 ActiveHeight;
    U32 ActiveBitsPerPixel;
    U32 ActiveStride;
    U32 ActiveSurfaceOffset;
    PHYSICAL FrameBufferPhysical;
    LINEAR FrameBufferLinear;
    U32 FrameBufferSize;
    GRAPHICSCONTEXT Context;
    INTEL_GFX_CAPS IntelCapabilities;
    GFX_CAPABILITIES Capabilities;
} INTEL_GFX_STATE, *LPINTEL_GFX_STATE;

/************************************************************************/

typedef struct tag_INTEL_GFX_MODE_PROGRAM {
    U32 PipeIndex;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    U32 RefreshRate;
    U32 PipeConf;
    U32 PipeSource;
    U32 PipeHTotal;
    U32 PipeHBlank;
    U32 PipeHSync;
    U32 PipeVTotal;
    U32 PipeVBlank;
    U32 PipeVSync;
    U32 PlaneControl;
    U32 PlaneStride;
    U32 PlaneSurface;
} INTEL_GFX_MODE_PROGRAM, *LPINTEL_GFX_MODE_PROGRAM;

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

static UINT IntelGfxCommands(UINT Function, UINT Param);

static DRIVER DATA_SECTION IntelGfxDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = INTEL_GFX_VER_MAJOR,
    .VersionMinor = INTEL_GFX_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "Intel Integrated Graphics",
    .Flags = 0,
    .Command = IntelGfxCommands
};

static INTEL_GFX_STATE DATA_SECTION IntelGfxState = {0};

/************************************************************************/

/**
 * @brief Retrieve Intel graphics driver descriptor.
 * @return Pointer to Intel graphics driver.
 */
LPDRIVER IntelGfxGetDriver(void) {
    return &IntelGfxDriver;
}

/************************************************************************/

/**
 * @brief Locate the first Intel display-class PCI device.
 * @return Matching PCI device pointer, or NULL if not found.
 */
static LPPCI_DEVICE IntelGfxFindDisplayDevice(void) {
    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            if (Device->Info.VendorID != INTEL_VENDOR_ID) {
                continue;
            }

            if (Device->Info.BaseClass != PCI_CLASS_DISPLAY) {
                continue;
            }

            return Device;
        }
    }

    return NULL;
}

/************************************************************************/

/************************************************************************/

/**
 * @brief Read a 32-bit MMIO register from Intel graphics BAR.
 * @param Offset Register offset.
 * @param ValueOut Receives register value.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL IntelGfxReadMmio32(U32 Offset, U32* ValueOut) {
    if (ValueOut == NULL) {
        return FALSE;
    }

    if (IntelGfxState.MmioBase == 0 || IntelGfxState.MmioSize < sizeof(U32)) {
        return FALSE;
    }

    if (Offset > IntelGfxState.MmioSize - sizeof(U32)) {
        return FALSE;
    }

    *ValueOut = *((volatile U32*)((U8*)(LINEAR)IntelGfxState.MmioBase + Offset));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Write a 32-bit MMIO register in Intel graphics BAR.
 * @param Offset Register offset.
 * @param Value Register value.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL IntelGfxWriteMmio32(U32 Offset, U32 Value) {
    if (IntelGfxState.MmioBase == 0 || IntelGfxState.MmioSize < sizeof(U32)) {
        return FALSE;
    }

    if (Offset > IntelGfxState.MmioSize - sizeof(U32)) {
        return FALSE;
    }

    *((volatile U32*)((U8*)(LINEAR)IntelGfxState.MmioBase + Offset)) = Value;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve Intel capability defaults from PCI device id family table.
 * @param DeviceId Intel PCI device id.
 * @param CapsOut Receives capability defaults.
 */
static void IntelGfxResolveCapabilitiesFromDevice(U16 DeviceId, LPINTEL_GFX_CAPS CapsOut) {
    UINT Index = 0;

    if (CapsOut == NULL) {
        return;
    }

    *CapsOut = (INTEL_GFX_CAPS){
        .Generation = 9,
        .DisplayVersion = 9,
        .PipeCount = 3,
        .TranscoderCount = 3,
        .PortMask = INTEL_PORT_A | INTEL_PORT_B | INTEL_PORT_C,
        .SupportsFBC = FALSE,
        .SupportsPSR = FALSE,
        .SupportsAsyncFlip = FALSE,
        .MaxWidth = 4096,
        .MaxHeight = 4096
    };

    for (Index = 0; Index < sizeof(IntelGfxFamilyTable) / sizeof(IntelGfxFamilyTable[0]); Index++) {
        const INTEL_GFX_FAMILY_ENTRY* Entry = &IntelGfxFamilyTable[Index];
        if ((DeviceId & Entry->DeviceIdMask) == Entry->DeviceId) {
            *CapsOut = (INTEL_GFX_CAPS){
                .Generation = Entry->Generation,
                .DisplayVersion = Entry->DisplayVersion,
                .PipeCount = Entry->PipeCount,
                .TranscoderCount = Entry->TranscoderCount,
                .PortMask = Entry->PortMask,
                .SupportsFBC = Entry->SupportsFBC,
                .SupportsPSR = Entry->SupportsPSR,
                .SupportsAsyncFlip = Entry->SupportsAsyncFlip,
                .MaxWidth = Entry->MaxWidth,
                .MaxHeight = Entry->MaxHeight
            };
            return;
        }
    }
}

/************************************************************************/

/**
 * @brief Probe display-related MMIO registers to refine capabilities.
 * @param CapsInOut Capability object updated in place.
 */
static void IntelGfxProbeCapabilities(LPINTEL_GFX_CAPS CapsInOut) {
    U32 Value = 0;
    U32 PipeCount = 0;
    U32 PortMask = 0;

    if (CapsInOut == NULL) {
        return;
    }

    if (IntelGfxReadMmio32(INTEL_REG_GMD_ID, &Value)) {
        U32 DisplayVersionMajor = (Value >> 4) & 0x0F;
        if (DisplayVersionMajor != 0 && DisplayVersionMajor != 0x0F) {
            CapsInOut->DisplayVersion = DisplayVersionMajor;
        }
    }

    if (IntelGfxReadMmio32(INTEL_REG_PIPE_A_CONF, &Value) && Value != 0xFFFFFFFF) {
        PipeCount++;
    }
    if (IntelGfxReadMmio32(INTEL_REG_PIPE_B_CONF, &Value) && Value != 0xFFFFFFFF) {
        PipeCount++;
    }
    if (IntelGfxReadMmio32(INTEL_REG_PIPE_C_CONF, &Value) && Value != 0xFFFFFFFF) {
        PipeCount++;
    }

    if (PipeCount != 0) {
        CapsInOut->PipeCount = PipeCount;
        if (CapsInOut->TranscoderCount < PipeCount) {
            CapsInOut->TranscoderCount = PipeCount;
        }
    }

    if (IntelGfxReadMmio32(INTEL_REG_DDI_BUF_CTL_A, &Value) && Value != 0xFFFFFFFF) PortMask |= INTEL_PORT_A;
    if (IntelGfxReadMmio32(INTEL_REG_DDI_BUF_CTL_B, &Value) && Value != 0xFFFFFFFF) PortMask |= INTEL_PORT_B;
    if (IntelGfxReadMmio32(INTEL_REG_DDI_BUF_CTL_C, &Value) && Value != 0xFFFFFFFF) PortMask |= INTEL_PORT_C;
    if (IntelGfxReadMmio32(INTEL_REG_DDI_BUF_CTL_D, &Value) && Value != 0xFFFFFFFF) PortMask |= INTEL_PORT_D;
    if (IntelGfxReadMmio32(INTEL_REG_DDI_BUF_CTL_E, &Value) && Value != 0xFFFFFFFF) PortMask |= INTEL_PORT_E;

    if (PortMask != 0) {
        CapsInOut->PortMask = PortMask;
    }
}

/************************************************************************/

/**
 * @brief Project Intel capability object to generic graphics capabilities.
 * @param IntelCaps Intel capability object.
 * @param GenericCaps Output generic capabilities.
 */
static void IntelGfxProjectCapabilities(const INTEL_GFX_CAPS* IntelCaps, LPGFX_CAPABILITIES GenericCaps) {
    if (IntelCaps == NULL || GenericCaps == NULL) {
        return;
    }

    *GenericCaps = (GFX_CAPABILITIES){
        .Header = {.Size = sizeof(GFX_CAPABILITIES), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .HasHardwareModeset = TRUE,
        .HasPageFlip = IntelCaps->SupportsAsyncFlip ? TRUE : FALSE,
        .HasVBlankInterrupt = (IntelCaps->PipeCount > 0) ? TRUE : FALSE,
        .HasCursorPlane = (IntelCaps->Generation >= 5) ? TRUE : FALSE,
        .SupportsTiledSurface = (IntelCaps->Generation >= 5) ? TRUE : FALSE,
        .MaxWidth = IntelCaps->MaxWidth,
        .MaxHeight = IntelCaps->MaxHeight,
        .PreferredFormat = GFX_FORMAT_XRGB8888
    };
}

/************************************************************************/

/**
 * @brief Resolve and cache Intel capability object from PCI+MMIO probes.
 * @param Device Active Intel PCI device.
 */
static void IntelGfxInitializeCapabilities(LPPCI_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    IntelGfxResolveCapabilitiesFromDevice(Device->Info.DeviceID, &IntelGfxState.IntelCapabilities);
    IntelGfxProbeCapabilities(&IntelGfxState.IntelCapabilities);
    IntelGfxProjectCapabilities(&IntelGfxState.IntelCapabilities, &IntelGfxState.Capabilities);

    DEBUG(TEXT("[IntelGfxInitializeCapabilities] Gen=%u Dv=%u Pipes=%u Transcoders=%u Ports=%x FBC=%u PSR=%u AsyncFlip=%u Max=%ux%u"),
        IntelGfxState.IntelCapabilities.Generation,
        IntelGfxState.IntelCapabilities.DisplayVersion,
        IntelGfxState.IntelCapabilities.PipeCount,
        IntelGfxState.IntelCapabilities.TranscoderCount,
        IntelGfxState.IntelCapabilities.PortMask,
        IntelGfxState.IntelCapabilities.SupportsFBC ? 1 : 0,
        IntelGfxState.IntelCapabilities.SupportsPSR ? 1 : 0,
        IntelGfxState.IntelCapabilities.SupportsAsyncFlip ? 1 : 0,
        IntelGfxState.IntelCapabilities.MaxWidth,
        IntelGfxState.IntelCapabilities.MaxHeight);
}

/************************************************************************/

/**
 * @brief Translate Intel plane pixel format to bits per pixel.
 * @param PlaneControlValue Value read from PLANE_CTL register.
 * @return Bits-per-pixel inferred from control value.
 */
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

/**
 * @brief Read active scanout state from pipe/plane registers.
 * @return TRUE when an active pipe/plane was found and parsed.
 */
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

/**
 * @brief Read conservative mode programming values for one pipe.
 * @param PipeIndex Pipe index (A/B/C mapped to 0/1/2).
 * @param ProgramOut Receives mode programming values.
 * @return TRUE on success, FALSE otherwise.
 */
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

/**
 * @brief Wait for pipe enable state change completion.
 * @param PipeIndex Target pipe.
 * @param EnabledExpected Expected enable state.
 * @return TRUE if expected state observed, FALSE on timeout/read error.
 */
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

/**
 * @brief Disable active plane and pipe before reprogramming.
 * @param PipeIndex Target pipe index.
 * @return DF_RETURN_SUCCESS on success.
 */
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

/**
 * @brief Program one conservative native mode and re-enable pipe.
 * @param Program Program description.
 * @return DF_RETURN_SUCCESS on success.
 */
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

/**
 * @brief Validate one conservative SETMODE request against Intel capabilities.
 * @param Info Requested mode description.
 * @param ProgramOut Receives programmed values.
 * @return DF_RETURN_SUCCESS on success, otherwise a graphics error code.
 */
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

/**
 * @brief Apply native conservative modeset on active pipe.
 * @param Program Program description.
 * @return DF_RETURN_SUCCESS on success.
 */
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

/**
 * @brief Map active scanout buffer through Intel aperture BAR.
 * @return TRUE on success, FALSE otherwise.
 */
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
            IntelGfxState.ActiveSurfaceOffset, Bar2Size);
        return FALSE;
    }

    if (IntelGfxState.FrameBufferSize > (Bar2Size - IntelGfxState.ActiveSurfaceOffset)) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] Frame buffer exceeds BAR2 window (size=%u available=%u)"),
            IntelGfxState.FrameBufferSize, Bar2Size - IntelGfxState.ActiveSurfaceOffset);
        return FALSE;
    }

    IntelGfxState.FrameBufferPhysical = (PHYSICAL)(Bar2Base + IntelGfxState.ActiveSurfaceOffset);
    IntelGfxState.FrameBufferLinear = MapIOMemory(IntelGfxState.FrameBufferPhysical, IntelGfxState.FrameBufferSize);
    if (IntelGfxState.FrameBufferLinear == 0) {
        ERROR(TEXT("[IntelGfxMapActiveFrameBuffer] MapIOMemory failed for base=%p size=%u"),
            (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical, IntelGfxState.FrameBufferSize);
        return FALSE;
    }

    DEBUG(TEXT("[IntelGfxMapActiveFrameBuffer] FrameBuffer=%p size=%u stride=%u"),
        (LPVOID)(LINEAR)IntelGfxState.FrameBufferPhysical,
        IntelGfxState.FrameBufferSize,
        IntelGfxState.ActiveStride);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build graphics context from active scanout takeover state.
 * @return TRUE on success, FALSE otherwise.
 */
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

/**
 * @brief Execute scanout takeover sequence from active Intel display state.
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
static UINT IntelGfxTakeoverActiveMode(void) {
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

/**
 * @brief Write a pixel in active Intel scanout buffer.
 * @param Context Graphics context.
 * @param X Target X coordinate.
 * @param Y Target Y coordinate.
 * @param Color Input color and output previous color.
 * @return TRUE on success.
 */
static BOOL IntelGfxWritePixel(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR* Color) {
    U8* Pixel = NULL;
    U32 Offset = 0;
    COLOR Previous = 0;

    if (Context == NULL || Color == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (X < Context->LoClip.X || X > Context->HiClip.X || Y < Context->LoClip.Y || Y > Context->HiClip.Y) {
        return FALSE;
    }

    if (Context->BitsPerPixel != 32) {
        return FALSE;
    }

    Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 2);
    Pixel = Context->MemoryBase + Offset;
    Previous = *((U32*)Pixel);
    *((U32*)Pixel) = *Color;
    *Color = Previous;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Draw a line with current pen in active scanout buffer.
 * @param Context Graphics context.
 * @param X1 Start X.
 * @param Y1 Start Y.
 * @param X2 End X.
 * @param Y2 End Y.
 */
static void IntelGfxDrawLine(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Dx = 0;
    I32 Sx = 0;
    I32 Dy = 0;
    I32 Sy = 0;
    I32 Error = 0;
    COLOR Color = 0;
    U32 Pattern = 0;
    U32 PatternBit = 0;

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
            (void)IntelGfxWritePixel(Context, X1, Y1, &PixelColor);
        }
        PatternBit++;

        if (X1 == X2 && Y1 == Y2) break;

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
 * @brief Fill and outline a rectangle with current brush/pen.
 * @param Context Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 */
static void IntelGfxDrawRectangle(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
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
                (void)IntelGfxWritePixel(Context, X, Y, &FillColor);
            }
        }
    }

    if (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN) {
        IntelGfxDrawLine(Context, X1, Y1, X2, Y1);
        IntelGfxDrawLine(Context, X2, Y1, X2, Y2);
        IntelGfxDrawLine(Context, X2, Y2, X1, Y2);
        IntelGfxDrawLine(Context, X1, Y2, X1, Y1);
    }
}

/************************************************************************/

/**
 * @brief Load Intel graphics driver and map MMIO BAR.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_UNEXPECTED otherwise.
 */
static UINT IntelGfxLoad(void) {
    LPPCI_DEVICE Device = NULL;
    U32 Bar0Base = 0;
    U32 Bar0Size = 0;
    U32 ProbeValue = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) != 0) {
        return DF_RETURN_SUCCESS;
    }

    Device = IntelGfxFindDisplayDevice();
    if (Device == NULL) {
        WARNING(TEXT("[IntelGfxLoad] No Intel display PCI device found"));
        return DF_RETURN_UNEXPECTED;
    }

    if (PCI_BAR_IS_IO(Device->Info.BAR[0])) {
        ERROR(TEXT("[IntelGfxLoad] BAR0 is I/O, expected MMIO (bar0=%x)"), Device->Info.BAR[0]);
        return DF_RETURN_UNEXPECTED;
    }

    Bar0Base = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    if (Bar0Base == 0 || Bar0Size == 0) {
        ERROR(TEXT("[IntelGfxLoad] Invalid BAR0 base=%x size=%u"), Bar0Base, Bar0Size);
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxState.MmioBase = MapIOMemory((PHYSICAL)Bar0Base, Bar0Size);
    if (IntelGfxState.MmioBase == 0) {
        ERROR(TEXT("[IntelGfxLoad] MapIOMemory failed for base=%p size=%u"), (LPVOID)(LINEAR)Bar0Base, Bar0Size);
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxState.MmioSize = Bar0Size;
    IntelGfxState.Device = Device;

    (void)PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    ProbeValue = *((volatile U32*)((U8*)(LINEAR)IntelGfxState.MmioBase + INTEL_MMIO_PROBE_REGISTER));
    DEBUG(TEXT("[IntelGfxLoad] Device=%x:%x.%u DID=%x BAR0=%p size=%u probe=%x"),
        Device->Info.Bus, Device->Info.Dev, Device->Info.Func, Device->Info.DeviceID,
        (LPVOID)(LINEAR)Bar0Base, Bar0Size, ProbeValue);

    IntelGfxInitializeCapabilities(Device);

    if (IntelGfxTakeoverActiveMode() != DF_RETURN_SUCCESS) {
        if (IntelGfxState.FrameBufferLinear != 0 && IntelGfxState.FrameBufferSize != 0) {
            UnMapIOMemory(IntelGfxState.FrameBufferLinear, IntelGfxState.FrameBufferSize);
        }
        if (IntelGfxState.MmioBase != 0 && IntelGfxState.MmioSize != 0) {
            UnMapIOMemory(IntelGfxState.MmioBase, IntelGfxState.MmioSize);
        }
        IntelGfxState = (INTEL_GFX_STATE){0};
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxDriver.Flags |= DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload Intel graphics driver and release MMIO mapping.
 * @return DF_RETURN_SUCCESS always.
 */
static UINT IntelGfxUnload(void) {
    if (IntelGfxState.FrameBufferLinear != 0 && IntelGfxState.FrameBufferSize != 0) {
        UnMapIOMemory(IntelGfxState.FrameBufferLinear, IntelGfxState.FrameBufferSize);
    }

    if (IntelGfxState.MmioBase != 0 && IntelGfxState.MmioSize != 0) {
        UnMapIOMemory(IntelGfxState.MmioBase, IntelGfxState.MmioSize);
    }

    IntelGfxState = (INTEL_GFX_STATE){0};
    IntelGfxDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Return active Intel graphics mode information.
 * @param Info Output mode descriptor.
 * @return DF_RETURN_SUCCESS when context mode is initialized.
 */
static UINT IntelGfxGetModeInfo(LPGRAPHICSMODEINFO Info) {
    SAFE_USE(Info) {
        if (IntelGfxState.Context.Width <= 0 || IntelGfxState.Context.Height <= 0 ||
            IntelGfxState.Context.BitsPerPixel == 0) {
            return DF_RETURN_UNEXPECTED;
        }

        Info->Width = (U32)IntelGfxState.Context.Width;
        Info->Height = (U32)IntelGfxState.Context.Height;
        Info->BitsPerPixel = IntelGfxState.Context.BitsPerPixel;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Execute active-mode takeover for SETMODE in step-4 path.
 * @param Info Requested/returned mode descriptor.
 * @return DF_RETURN_SUCCESS on takeover success.
 */
static UINT IntelGfxSetMode(LPGRAPHICSMODEINFO Info) {
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

/**
 * @brief Return Intel graphics capabilities.
 * @param Capabilities Output capabilities descriptor.
 * @return DF_RETURN_SUCCESS on success.
 */
static UINT IntelGfxGetCapabilities(LPGFX_CAPABILITIES Capabilities) {
    SAFE_USE(Capabilities) {
        *Capabilities = IntelGfxState.Capabilities;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Set a pixel in active Intel scanout buffer.
 * @param Info Pixel descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxSetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    COLOR PixelColor = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    PixelColor = Info->Color;

    LockMutex(&(Context->Mutex), INFINITY);
    if (!IntelGfxWritePixel(Context, Info->X, Info->Y, &PixelColor)) {
        UnlockMutex(&(Context->Mutex));
        return 0;
    }
    UnlockMutex(&(Context->Mutex));
    Info->Color = PixelColor;
    return 1;
}

/************************************************************************/

/**
 * @brief Read a pixel from active Intel scanout buffer.
 * @param Info Pixel descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxGetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    U32 Offset = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT || Context->MemoryBase == NULL) {
        return 0;
    }

    if (Context->BitsPerPixel != 32) {
        return 0;
    }

    if (Info->X < Context->LoClip.X || Info->X > Context->HiClip.X || Info->Y < Context->LoClip.Y || Info->Y > Context->HiClip.Y) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Offset = (U32)(Info->Y * (I32)Context->BytesPerScanLine) + ((U32)Info->X << 2);
    Info->Color = *((U32*)(Context->MemoryBase + Offset));
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

/**
 * @brief Draw a line in active Intel scanout buffer.
 * @param Info Line descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxLine(LPLINEINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawLine(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

/**
 * @brief Draw a rectangle in active Intel scanout buffer.
 * @param Info Rectangle descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxRectangle(LPRECTINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawRectangle(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

/**
 * @brief Draw one text cell in active Intel scanout.
 * @param Info Text cell descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxTextPutCell(LPGFX_TEXT_CELL_INFO Info) {
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
 * @brief Clear one text region in active Intel scanout.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxTextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
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
 * @brief Scroll one text region in active Intel scanout.
 * @param Info Text region descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxTextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
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
 * @brief Draw cursor in active Intel scanout.
 * @param Info Cursor descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxTextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
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
 * @brief Set cursor visibility in Intel backend.
 * @param Info Cursor visibility descriptor.
 * @return 1 on success, 0 on failure.
 */
static UINT IntelGfxTextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
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
 * @brief Present path for takeover mode (CPU draw directly to scanout).
 * @param Info Present descriptor.
 * @return DF_RETURN_SUCCESS when scanout buffer is active.
 */
static UINT IntelGfxPresent(LPGFX_PRESENT_INFO Info) {
    UNUSED(Info);

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Intel graphics command dispatcher.
 * @param Function Driver function code.
 * @param Param Function parameter.
 * @return Driver result code.
 */
static UINT IntelGfxCommands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return IntelGfxLoad();
        case DF_UNLOAD:
            return IntelGfxUnload();
        case DF_GET_VERSION:
            return MAKE_VERSION(INTEL_GFX_VER_MAJOR, INTEL_GFX_VER_MINOR);

        case DF_GFX_CREATECONTEXT:
            if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return 0;
            }
            return (UINT)(LPVOID)&IntelGfxState.Context;
        case DF_GFX_GETMODEINFO:
            return IntelGfxGetModeInfo((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_GETCAPABILITIES:
            return IntelGfxGetCapabilities((LPGFX_CAPABILITIES)Param);
        case DF_GFX_SETMODE:
            return IntelGfxSetMode((LPGRAPHICSMODEINFO)Param);
        case DF_GFX_SETPIXEL:
            return IntelGfxSetPixel((LPPIXELINFO)Param);
        case DF_GFX_GETPIXEL:
            return IntelGfxGetPixel((LPPIXELINFO)Param);
        case DF_GFX_LINE:
            return IntelGfxLine((LPLINEINFO)Param);
        case DF_GFX_RECTANGLE:
            return IntelGfxRectangle((LPRECTINFO)Param);
        case DF_GFX_TEXT_PUTCELL:
            return IntelGfxTextPutCell((LPGFX_TEXT_CELL_INFO)Param);
        case DF_GFX_TEXT_CLEAR_REGION:
            return IntelGfxTextClearRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SCROLL_REGION:
            return IntelGfxTextScrollRegion((LPGFX_TEXT_REGION_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR:
            return IntelGfxTextSetCursor((LPGFX_TEXT_CURSOR_INFO)Param);
        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
            return IntelGfxTextSetCursorVisible((LPGFX_TEXT_CURSOR_VISIBLE_INFO)Param);
        case DF_GFX_PRESENT:
            return IntelGfxPresent((LPGFX_PRESENT_INFO)Param);

        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_ELLIPSE:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_WAITVBLANK:
        case DF_GFX_ALLOCSURFACE:
        case DF_GFX_FREESURFACE:
        case DF_GFX_SETSCANOUT:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
