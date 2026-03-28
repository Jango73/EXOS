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


    DMA buffer helper

\************************************************************************/

#ifndef DMABUFFER_H_INCLUDED
#define DMABUFFER_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

typedef struct tag_DMA_BUFFER {
    LINEAR LinearBase;
    PHYSICAL PhysicalBase;
    UINT Size;
    UINT AllocatedSize;
    BOOL IsContiguous;
} DMA_BUFFER, *LPDMA_BUFFER;

/***************************************************************************/

BOOL DMABufferAllocate(LPDMA_BUFFER Buffer, UINT Size, BOOL RequireContiguous, LPCSTR Tag);
void DMABufferRelease(LPDMA_BUFFER Buffer);
PHYSICAL DMABufferGetPhysical(const DMA_BUFFER* Buffer, UINT Offset);

/***************************************************************************/

#endif  // DMABUFFER_H_INCLUDED
