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


    RectRegion helper

\************************************************************************/

#ifndef RECTREGION_H_INCLUDED
#define RECTREGION_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "User.h"

/************************************************************************/
// Type definitions

typedef struct tag_RECT_REGION {
    LPRECT Storage;
    UINT Capacity;
    UINT Count;
    BOOL Overflowed;
} RECT_REGION, *LPRECT_REGION;

/************************************************************************/
// External functions

BOOL RectRegionInit(LPRECT_REGION Region, LPRECT Storage, UINT Capacity);
void RectRegionReset(LPRECT_REGION Region);
BOOL RectRegionAddRect(LPRECT_REGION Region, LPRECT Rect);
BOOL RectRegionUnionRect(LPRECT_REGION Region, LPRECT Rect);
BOOL RectRegionIntersectRect(LPRECT_REGION Region, LPRECT Rect);
UINT RectRegionGetCount(LPRECT_REGION Region);
BOOL RectRegionGetRect(LPRECT_REGION Region, UINT Index, LPRECT Rect);
BOOL RectRegionIsOverflowed(LPRECT_REGION Region);

/************************************************************************/

#endif

