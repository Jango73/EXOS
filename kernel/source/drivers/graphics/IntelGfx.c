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
#include "KernelData.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/bus/PCI.h"

/************************************************************************/

#define INTEL_GFX_VER_MAJOR 1
#define INTEL_GFX_VER_MINOR 0

#define INTEL_VENDOR_ID 0x8086
#define INTEL_MMIO_PROBE_REGISTER 0x0000

/************************************************************************/

typedef struct tag_INTEL_GFX_STATE {
    LPPCI_DEVICE Device;
    LINEAR MmioBase;
    U32 MmioSize;
    GRAPHICSCONTEXT Context;
    GFX_CAPABILITIES Capabilities;
} INTEL_GFX_STATE, *LPINTEL_GFX_STATE;

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

/**
 * @brief Initialize static capabilities for the Intel skeleton driver.
 * @param State Driver state to initialize.
 */
static void IntelGfxInitializeCapabilities(LPINTEL_GFX_STATE State) {
    if (State == NULL) {
        return;
    }

    State->Capabilities = (GFX_CAPABILITIES){
        .Header = {.Size = sizeof(GFX_CAPABILITIES), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .HasHardwareModeset = TRUE,
        .HasPageFlip = FALSE,
        .HasVBlankInterrupt = FALSE,
        .HasCursorPlane = FALSE,
        .SupportsTiledSurface = FALSE,
        .MaxWidth = 8192,
        .MaxHeight = 8192,
        .PreferredFormat = GFX_FORMAT_XRGB8888
    };
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

    IntelGfxState.Context = (GRAPHICSCONTEXT){
        .TypeID = KOID_GRAPHICSCONTEXT,
        .References = 1,
        .Mutex = EMPTY_MUTEX,
        .Driver = &IntelGfxDriver,
        .LoClip = {.X = 0, .Y = 0},
        .HiClip = {.X = 0, .Y = 0},
        .Origin = {.X = 0, .Y = 0},
        .RasterOperation = ROP_SET
    };

    IntelGfxInitializeCapabilities(&IntelGfxState);

    IntelGfxDriver.Flags |= DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload Intel graphics driver and release MMIO mapping.
 * @return DF_RETURN_SUCCESS always.
 */
static UINT IntelGfxUnload(void) {
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
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
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
