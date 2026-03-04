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


    Intel graphics (family operations)

\************************************************************************/

#include "iGPU-Internal.h"

/************************************************************************/

static const INTEL_DISPLAY_FAMILY_OPS IntelGfxFamilyOpsTable[] = {
    {.DisplayVersionMin = 0,
        .DisplayVersionMax = 6,
        .Name = TEXT("IntelDisplayV6"),
        .StrideWriteMask = 0x0001FFFC,
        .StrideReadMask = 0x0001FFFC,
        .StrideAlignment = 64,
        .StrideEncodeDivisor = 64,
        .StrideReadMultiplierPrimary = 64,
        .StrideReadMultiplierSecondary = 1,
        .SurfaceAlignment = 0x1000,
        .ColdSurfaceOffset = 0,
        .CompressionControlRegister = INTEL_REG_FBC_CONTROL,
        .CompressionControlEnableMask = (1 << 31),
        .RequireCompressionDisable = TRUE,
        .ForceLinearPlaneTiling = FALSE,
        .SupportsColdModeset = TRUE},
    {.DisplayVersionMin = 7,
        .DisplayVersionMax = 8,
        .Name = TEXT("IntelDisplayV8"),
        .StrideWriteMask = 0x0001FFFC,
        .StrideReadMask = 0x0001FFFC,
        .StrideAlignment = 64,
        .StrideEncodeDivisor = 64,
        .StrideReadMultiplierPrimary = 64,
        .StrideReadMultiplierSecondary = 1,
        .SurfaceAlignment = 0x1000,
        .ColdSurfaceOffset = 0,
        .CompressionControlRegister = INTEL_REG_FBC_CONTROL,
        .CompressionControlEnableMask = (1 << 31),
        .RequireCompressionDisable = TRUE,
        .ForceLinearPlaneTiling = FALSE,
        .SupportsColdModeset = TRUE},
    {.DisplayVersionMin = 9,
        .DisplayVersionMax = 10,
        .Name = TEXT("IntelDisplayV9_10"),
        .StrideWriteMask = 0x0001FFFC,
        .StrideReadMask = 0x0001FFFC,
        .StrideAlignment = 64,
        .StrideEncodeDivisor = 64,
        .StrideReadMultiplierPrimary = 64,
        .StrideReadMultiplierSecondary = 1,
        .SurfaceAlignment = 0x1000,
        .ColdSurfaceOffset = 0,
        .CompressionControlRegister = INTEL_REG_FBC_CONTROL,
        .CompressionControlEnableMask = (1 << 31),
        .RequireCompressionDisable = TRUE,
        .ForceLinearPlaneTiling = TRUE,
        .SupportsColdModeset = TRUE},
    {.DisplayVersionMin = 11,
        .DisplayVersionMax = 11,
        .Name = TEXT("IntelDisplayV11"),
        .StrideWriteMask = 0x0001FFFC,
        .StrideReadMask = 0x0001FFFC,
        .StrideAlignment = 64,
        .StrideEncodeDivisor = 64,
        .StrideReadMultiplierPrimary = 64,
        .StrideReadMultiplierSecondary = 1,
        .SurfaceAlignment = 0x1000,
        .ColdSurfaceOffset = 0,
        .CompressionControlRegister = INTEL_REG_FBC_CONTROL,
        .CompressionControlEnableMask = (1 << 31),
        .RequireCompressionDisable = TRUE,
        .ForceLinearPlaneTiling = TRUE,
        .SupportsColdModeset = TRUE},
    {.DisplayVersionMin = 12,
        .DisplayVersionMax = MAX_U32,
        .Name = TEXT("IntelDisplayV12Plus"),
        .StrideWriteMask = 0x0001FFFC,
        .StrideReadMask = 0x0001FFFC,
        .StrideAlignment = 64,
        .StrideEncodeDivisor = 64,
        .StrideReadMultiplierPrimary = 64,
        .StrideReadMultiplierSecondary = 1,
        .SurfaceAlignment = 0x1000,
        .ColdSurfaceOffset = 0,
        .CompressionControlRegister = INTEL_REG_FBC_CONTROL,
        .CompressionControlEnableMask = (1 << 31),
        .RequireCompressionDisable = TRUE,
        .ForceLinearPlaneTiling = TRUE,
        .SupportsColdModeset = TRUE}
};

/************************************************************************/

static U32 IntelGfxAlignUp(U32 Value, U32 Alignment) {
    U32 Mask = 0;

    if (Alignment <= 1) {
        return Value;
    }

    Mask = Alignment - 1;
    return (Value + Mask) & ~Mask;
}

/************************************************************************/

const INTEL_DISPLAY_FAMILY_OPS* IntelGfxGetFamilyProgramming(void) {
    U32 DisplayVersion = IntelGfxState.IntelCapabilities.DisplayVersion;
    UINT Index = 0;

    for (Index = 0; Index < sizeof(IntelGfxFamilyOpsTable) / sizeof(IntelGfxFamilyOpsTable[0]); Index++) {
        const INTEL_DISPLAY_FAMILY_OPS* Family = &IntelGfxFamilyOpsTable[Index];
        if (DisplayVersion >= Family->DisplayVersionMin && DisplayVersion <= Family->DisplayVersionMax) {
            return Family;
        }
    }

    return NULL;
}

/************************************************************************/

U32 IntelGfxResolveStrideFromReadback(U32 ReadBackStride, U32 Width, U32 BitsPerPixel) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 MinimumStride = 0;
    U32 Candidate = 0;

    if (Family == NULL || Width == 0 || BitsPerPixel == 0) {
        return 0;
    }

    MinimumStride = Width * (BitsPerPixel >> 3);
    Candidate = (ReadBackStride & Family->StrideReadMask) * Family->StrideReadMultiplierPrimary;
    if (Candidate >= MinimumStride) {
        return IntelGfxAlignUp(Candidate, Family->StrideAlignment);
    }

    if (Family->StrideReadMultiplierSecondary > 1) {
        Candidate = (ReadBackStride & Family->StrideReadMask) * Family->StrideReadMultiplierSecondary;
        if (Candidate >= MinimumStride) {
            return IntelGfxAlignUp(Candidate, Family->StrideAlignment);
        }
    }

    return IntelGfxAlignUp(MinimumStride, Family->StrideAlignment);
}

/************************************************************************/

U32 IntelGfxBuildProgramStride(U32 Width, U32 BitsPerPixel) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 MinimumStride = 0;

    if (Family == NULL || Width == 0 || BitsPerPixel == 0) {
        return 0;
    }

    MinimumStride = Width * (BitsPerPixel >> 3);
    return IntelGfxAlignUp(MinimumStride, Family->StrideAlignment);
}

/************************************************************************/

U32 IntelGfxEncodeProgramStride(U32 StrideBytes) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();
    U32 AlignedStride = 0;
    U32 EncodedStride = 0;

    if (Family == NULL || Family->StrideEncodeDivisor == 0 || StrideBytes == 0) {
        return 0;
    }

    AlignedStride = IntelGfxAlignUp(StrideBytes, Family->StrideAlignment);
    EncodedStride = AlignedStride / Family->StrideEncodeDivisor;
    return EncodedStride & Family->StrideWriteMask;
}

/************************************************************************/

U32 IntelGfxBuildPlaneControl(U32 PlaneControlBase) {
    const INTEL_DISPLAY_FAMILY_OPS* Family = IntelGfxGetFamilyProgramming();

    if (Family == NULL) {
        return PlaneControlBase;
    }

    PlaneControlBase &= ~INTEL_PLANE_CTL_FORMAT_MASK;
    PlaneControlBase |= INTEL_PLANE_CTL_FORMAT_XRGB8888;

    if (Family->ForceLinearPlaneTiling != FALSE) {
        PlaneControlBase &= ~INTEL_PLANE_CTL_TILING_MASK;
        PlaneControlBase |= INTEL_PLANE_CTL_TILING_LINEAR;
    }

    return PlaneControlBase;
}

/************************************************************************/
