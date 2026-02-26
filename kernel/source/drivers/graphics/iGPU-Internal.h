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


    Intel graphics internal shared declarations

\************************************************************************/

#ifndef IGPU_INTERNAL_H_INCLUDED
#define IGPU_INTERNAL_H_INCLUDED

/************************************************************************/

#include "GFX.h"
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
#define INTEL_GFX_MAX_SURFACES 8
#define INTEL_GFX_SURFACE_FIRST_ID 1

#define DF_RETURN_IGFX_NO_DISPLAY_DEVICE (DF_RETURN_FIRST + 0x300)
#define DF_RETURN_IGFX_INVALID_BAR0 (DF_RETURN_FIRST + 0x301)
#define DF_RETURN_IGFX_MAP_MMIO_FAILED (DF_RETURN_FIRST + 0x302)
#define DF_RETURN_IGFX_NO_ACTIVE_SCANOUT (DF_RETURN_FIRST + 0x303)
#define DF_RETURN_IGFX_MAP_FRAMEBUFFER_FAILED (DF_RETURN_FIRST + 0x304)
#define DF_RETURN_IGFX_BUILD_CONTEXT_FAILED (DF_RETURN_FIRST + 0x305)

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
    U32 NextSurfaceId;
    U32 ScanoutSurfaceId;
    U32 PresentBlitCount;
} INTEL_GFX_STATE, *LPINTEL_GFX_STATE;

/************************************************************************/

typedef struct tag_INTEL_GFX_SURFACE {
    BOOL InUse;
    U32 SurfaceId;
    U32 Width;
    U32 Height;
    U32 Format;
    U32 Pitch;
    U32 Flags;
    U32 SizeBytes;
    U8* MemoryBase;
} INTEL_GFX_SURFACE, *LPINTEL_GFX_SURFACE;

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

extern DRIVER IntelGfxDriver;
extern INTEL_GFX_STATE IntelGfxState;

/************************************************************************/

LPDRIVER IntelGfxGetDriver(void);

BOOL IntelGfxReadMmio32(U32 Offset, U32* ValueOut);
BOOL IntelGfxWriteMmio32(U32 Offset, U32 Value);

UINT IntelGfxTakeoverActiveMode(void);
UINT IntelGfxSetMode(LPGRAPHICSMODEINFO Info);

void IntelGfxReleaseAllSurfaces(void);

UINT IntelGfxSetPixel(LPPIXELINFO Info);
UINT IntelGfxGetPixel(LPPIXELINFO Info);
UINT IntelGfxLine(LPLINEINFO Info);
UINT IntelGfxRectangle(LPRECTINFO Info);
UINT IntelGfxTextPutCell(LPGFX_TEXT_CELL_INFO Info);
UINT IntelGfxTextClearRegion(LPGFX_TEXT_REGION_INFO Info);
UINT IntelGfxTextScrollRegion(LPGFX_TEXT_REGION_INFO Info);
UINT IntelGfxTextSetCursor(LPGFX_TEXT_CURSOR_INFO Info);
UINT IntelGfxTextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info);
UINT IntelGfxPresent(LPGFX_PRESENT_INFO Info);
UINT IntelGfxAllocateSurface(LPGFX_SURFACE_INFO Info);
UINT IntelGfxFreeSurface(LPGFX_SURFACE_INFO Info);
UINT IntelGfxSetScanout(LPGFX_SCANOUT_INFO Info);

/************************************************************************/

#endif
