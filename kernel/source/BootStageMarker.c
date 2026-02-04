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


    Boot stage markers

\************************************************************************/

#include "BootStageMarker.h"
#include "Console.h"

/************************************************************************/

typedef struct tag_BOOT_STAGE_MARKER_FRAMEBUFFER_INFO {
    U8* Linear;
    U32 Pitch;
    U32 Width;
    U32 Height;
    U32 RedPosition;
    U32 RedMaskSize;
    U32 GreenPosition;
    U32 GreenMaskSize;
    U32 BluePosition;
    U32 BlueMaskSize;
} BOOT_STAGE_MARKER_FRAMEBUFFER_INFO;

/************************************************************************/

#ifndef BOOT_STAGE_MARKERS
#define BOOT_STAGE_MARKERS 0
#endif

/************************************************************************/

static U32 BootStageMarkerScaleChannel(U32 Value, U32 MaskSize);
static U32 BootStageMarkerComposeColor(const BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo, U32 Red, U32 Green, U32 Blue);
static BOOL BootStageMarkerBuildFromConsole(BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo);
static BOOL BootStageMarkerBuildFromMultiboot(
    const multiboot_info_t* MultibootInfo,
    BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo);
static void BootStageMarkerDraw(
    const BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo,
    U32 StageIndex,
    U32 Red,
    U32 Green,
    U32 Blue);

/************************************************************************/

/**
 * @brief Draw one stage marker using the mapped console framebuffer.
 *
 * @param StageIndex Marker index on the first marker line.
 * @param Red Red channel in range [0, 255].
 * @param Green Green channel in range [0, 255].
 * @param Blue Blue channel in range [0, 255].
 */
void BootStageMarkerFromConsole(U32 StageIndex, U32 Red, U32 Green, U32 Blue) {
#if BOOT_STAGE_MARKERS == 1
    BOOT_STAGE_MARKER_FRAMEBUFFER_INFO FramebufferInfo;
    if (BootStageMarkerBuildFromConsole(&FramebufferInfo) == FALSE) {
        return;
    }

    BootStageMarkerDraw(&FramebufferInfo, StageIndex, Red, Green, Blue);
#else
    UNUSED(StageIndex);
    UNUSED(Red);
    UNUSED(Green);
    UNUSED(Blue);
#endif
}

/************************************************************************/

/**
 * @brief Draw one stage marker using multiboot framebuffer information.
 *
 * @param MultibootInfo Multiboot information pointer.
 * @param StageIndex Marker index on the first marker line.
 * @param Red Red channel in range [0, 255].
 * @param Green Green channel in range [0, 255].
 * @param Blue Blue channel in range [0, 255].
 */
void BootStageMarkerFromMultiboot(const multiboot_info_t* MultibootInfo, U32 StageIndex, U32 Red, U32 Green, U32 Blue) {
#if BOOT_STAGE_MARKERS == 1
    BOOT_STAGE_MARKER_FRAMEBUFFER_INFO FramebufferInfo;
    if (BootStageMarkerBuildFromMultiboot(MultibootInfo, &FramebufferInfo) == FALSE) {
        return;
    }

    BootStageMarkerDraw(&FramebufferInfo, StageIndex, Red, Green, Blue);
#else
    UNUSED(MultibootInfo);
    UNUSED(StageIndex);
    UNUSED(Red);
    UNUSED(Green);
    UNUSED(Blue);
#endif
}

/************************************************************************/

/**
 * @brief Scale an 8-bit channel value to framebuffer mask size.
 *
 * @param Value Channel value in range [0,255].
 * @param MaskSize Number of bits in the destination channel.
 * @return Channel value scaled to the mask.
 */
static U32 BootStageMarkerScaleChannel(U32 Value, U32 MaskSize) {
    if (MaskSize == 0) {
        return 0;
    }

    if (MaskSize >= 8) {
        return Value & 0xFF;
    }

    U32 MaxValue = (1 << MaskSize) - 1;
    return (Value * MaxValue) / 255;
}

/************************************************************************/

/**
 * @brief Build one packed pixel value from RGB channels.
 *
 * @param FramebufferInfo Framebuffer layout.
 * @param Red Red channel in range [0,255].
 * @param Green Green channel in range [0,255].
 * @param Blue Blue channel in range [0,255].
 * @return Packed pixel value.
 */
static U32 BootStageMarkerComposeColor(const BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo, U32 Red, U32 Green, U32 Blue) {
    U32 Pixel = 0;

    Pixel |= BootStageMarkerScaleChannel(Red, FramebufferInfo->RedMaskSize) << FramebufferInfo->RedPosition;
    Pixel |= BootStageMarkerScaleChannel(Green, FramebufferInfo->GreenMaskSize) << FramebufferInfo->GreenPosition;
    Pixel |= BootStageMarkerScaleChannel(Blue, FramebufferInfo->BlueMaskSize) << FramebufferInfo->BluePosition;

    return Pixel;
}

/************************************************************************/

/**
 * @brief Build a framebuffer descriptor from the global console state.
 *
 * @param FramebufferInfo Receives framebuffer descriptor.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL BootStageMarkerBuildFromConsole(BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo) {
    if (FramebufferInfo == NULL) {
        return FALSE;
    }

    if (Console.FramebufferType != MULTIBOOT_FRAMEBUFFER_RGB ||
        Console.FramebufferBitsPerPixel != 32 ||
        Console.FramebufferBytesPerPixel != 4 ||
        Console.FramebufferPitch == 0 ||
        Console.FramebufferWidth == 0 ||
        Console.FramebufferHeight == 0 ||
        Console.FramebufferLinear == NULL) {
        return FALSE;
    }

    FramebufferInfo->Linear = Console.FramebufferLinear;
    FramebufferInfo->Pitch = Console.FramebufferPitch;
    FramebufferInfo->Width = Console.FramebufferWidth;
    FramebufferInfo->Height = Console.FramebufferHeight;
    FramebufferInfo->RedPosition = Console.FramebufferRedPosition;
    FramebufferInfo->RedMaskSize = Console.FramebufferRedMaskSize;
    FramebufferInfo->GreenPosition = Console.FramebufferGreenPosition;
    FramebufferInfo->GreenMaskSize = Console.FramebufferGreenMaskSize;
    FramebufferInfo->BluePosition = Console.FramebufferBluePosition;
    FramebufferInfo->BlueMaskSize = Console.FramebufferBlueMaskSize;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build a framebuffer descriptor from multiboot data.
 *
 * @param MultibootInfo Multiboot info pointer.
 * @param FramebufferInfo Receives framebuffer descriptor.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL BootStageMarkerBuildFromMultiboot(
    const multiboot_info_t* MultibootInfo,
    BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo) {
    if (MultibootInfo == NULL || FramebufferInfo == NULL) {
        return FALSE;
    }

    if ((MultibootInfo->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) == 0 ||
        MultibootInfo->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB ||
        MultibootInfo->framebuffer_bpp != 32 ||
        MultibootInfo->framebuffer_pitch == 0 ||
        MultibootInfo->framebuffer_width == 0 ||
        MultibootInfo->framebuffer_height == 0 ||
        MultibootInfo->framebuffer_addr_high != 0) {
        return FALSE;
    }

    FramebufferInfo->Linear = (U8*)(UINT)MultibootInfo->framebuffer_addr_low;
    if (FramebufferInfo->Linear == NULL) {
        return FALSE;
    }

    FramebufferInfo->Pitch = MultibootInfo->framebuffer_pitch;
    FramebufferInfo->Width = MultibootInfo->framebuffer_width;
    FramebufferInfo->Height = MultibootInfo->framebuffer_height;
    FramebufferInfo->RedPosition = MultibootInfo->color_info[0];
    FramebufferInfo->RedMaskSize = MultibootInfo->color_info[1];
    FramebufferInfo->GreenPosition = MultibootInfo->color_info[2];
    FramebufferInfo->GreenMaskSize = MultibootInfo->color_info[3];
    FramebufferInfo->BluePosition = MultibootInfo->color_info[4];
    FramebufferInfo->BlueMaskSize = MultibootInfo->color_info[5];

    return TRUE;
}

/************************************************************************/

/**
 * @brief Draw a square colored marker on the first marker line.
 *
 * @param FramebufferInfo Framebuffer descriptor.
 * @param StageIndex Marker index on the first line.
 * @param Red Red channel in range [0,255].
 * @param Green Green channel in range [0,255].
 * @param Blue Blue channel in range [0,255].
 */
static void BootStageMarkerDraw(
    const BOOT_STAGE_MARKER_FRAMEBUFFER_INFO* FramebufferInfo,
    U32 StageIndex,
    U32 Red,
    U32 Green,
    U32 Blue) {
    if (FramebufferInfo == NULL) {
        return;
    }

    const U32 MarkerBaseX = 2;
    const U32 MarkerBaseY = 2;
    const U32 MarkerSize = 8;
    const U32 MarkerStride = 10;
    const U32 MarkerLineStride = 10;
    const U32 GroupSize = 10;

    U32 GroupIndex = StageIndex / GroupSize;
    U32 GroupOffset = StageIndex % GroupSize;
    U32 StartX = MarkerBaseX + (GroupOffset * MarkerStride);
    U32 StartY = MarkerBaseY + (GroupIndex * MarkerLineStride);
    if (StartX >= FramebufferInfo->Width || StartY >= FramebufferInfo->Height) {
        return;
    }

    U32 DrawWidth = MarkerSize;
    U32 DrawHeight = MarkerSize;
    if (StartX + DrawWidth > FramebufferInfo->Width) {
        DrawWidth = FramebufferInfo->Width - StartX;
    }
    if (StartY + DrawHeight > FramebufferInfo->Height) {
        DrawHeight = FramebufferInfo->Height - StartY;
    }

    U32 Pixel = BootStageMarkerComposeColor(FramebufferInfo, Red, Green, Blue);

    for (U32 Y = 0; Y < DrawHeight; Y++) {
        U32* Row = (U32*)(FramebufferInfo->Linear + ((StartY + Y) * FramebufferInfo->Pitch) + (StartX * 4));
        for (U32 X = 0; X < DrawWidth; X++) {
            Row[X] = Pixel;
        }
    }
}
