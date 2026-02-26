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


    Intel graphics (base, load and dispatch)

\************************************************************************/

#include "iGPU-Internal.h"

#include "KernelData.h"
#include "Log.h"
#include "Memory.h"

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

static UINT IntelGfxCommands(UINT Function, UINT Param);

DRIVER DATA_SECTION IntelGfxDriver = {
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

INTEL_GFX_STATE DATA_SECTION IntelGfxState = {0};

/************************************************************************/

LPDRIVER IntelGfxGetDriver(void) {
    return &IntelGfxDriver;
}

/************************************************************************/

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

BOOL IntelGfxReadMmio32(U32 Offset, U32* ValueOut) {
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

BOOL IntelGfxWriteMmio32(U32 Offset, U32 Value) {
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

    if (IntelGfxReadMmio32(INTEL_REG_PIPE_A_CONF, &Value) && Value != 0xFFFFFFFF) PipeCount++;
    if (IntelGfxReadMmio32(INTEL_REG_PIPE_B_CONF, &Value) && Value != 0xFFFFFFFF) PipeCount++;
    if (IntelGfxReadMmio32(INTEL_REG_PIPE_C_CONF, &Value) && Value != 0xFFFFFFFF) PipeCount++;

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
    IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;
    IntelGfxState.ScanoutSurfaceId = 0;
    IntelGfxState.PresentBlitCount = 0;

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
        IntelGfxReleaseAllSurfaces();
        IntelGfxState = (INTEL_GFX_STATE){0};
        return DF_RETURN_UNEXPECTED;
    }

    IntelGfxDriver.Flags |= DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static UINT IntelGfxUnload(void) {
    IntelGfxReleaseAllSurfaces();

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

static UINT IntelGfxGetModeInfo(LPGRAPHICSMODEINFO Info) {
    SAFE_USE(Info) {
        if (IntelGfxState.Context.Width <= 0 || IntelGfxState.Context.Height <= 0 || IntelGfxState.Context.BitsPerPixel == 0) {
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

static UINT IntelGfxGetCapabilities(LPGFX_CAPABILITIES Capabilities) {
    SAFE_USE(Capabilities) {
        *Capabilities = IntelGfxState.Capabilities;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

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
        case DF_GFX_ALLOCSURFACE:
            return IntelGfxAllocateSurface((LPGFX_SURFACE_INFO)Param);
        case DF_GFX_FREESURFACE:
            return IntelGfxFreeSurface((LPGFX_SURFACE_INFO)Param);
        case DF_GFX_SETSCANOUT:
            return IntelGfxSetScanout((LPGFX_SCANOUT_INFO)Param);

        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_ELLIPSE:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_WAITVBLANK:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
